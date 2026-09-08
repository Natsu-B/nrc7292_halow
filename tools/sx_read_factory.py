#!/usr/bin/env python3
"""SX ROM PING and tightly bounded factory readback; no flash programming commands.

The default `probe` action sends one vendor PING at 115200. It never loads RAM,
runs code, toggles reset, or prints bytes received from the target.

`ram-info` and `ram-read-factory` require ROM download mode. They upload/run the
exact audited vendor loader in RAM, temporarily change one RAM map pointer, and
restore it. Loader startup may send flash status/unlock commands; this helper
sends no flash program/erase/migration commands. Power-cycle after the RAM session.
"""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import tempfile
import termios
import time
import zlib

SX_SERIAL = '08db3e3b2974ea11adf500fb6a30b0f3'
SX_PORT = '/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_' + SX_SERIAL + '-if00-port0'
BOOT_PATH = Path('/home/hotaru/silex/_codex_work/flash/FirmwareFlashTool/firmware/nrc7292_boot.bin')
BOOT_SHA256 = '33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057'
BOOT_ADDRESS = 0x10488000
BOOT_SIZE = 10680
MAP_ADDRESS = 0x10498034
SYS_CONFIG_POINTER = MAP_ADDRESS + 32
GEOMETRY_OFFSET = 0xff49806c  # +0x11000000 wraps to fixed sf_info_t RAM0x1049806c.
JEDEC_STACK_OFFSET = 0xff4b7fb8  # Fixed wait frame in this exact loader only.
LEGACY_MAC_OFFSET = 0xffffe000  # Exact vendor migration literal; may be unmapped.
PRIVATE_ROOT = Path('/home/hotaru/silex/_codex_work/private_sx_backups')
FLASH_SIZES = (0x100000, 0x200000, 0x400000, 0x1000000)
MAP_FIELDS = ('profile_version', 'firmware', 'firmware_info', 'core_dump',
              'user_config_1', 'user_config_2', 'user_config_3', 'user_config_4',
              'sys_config', 'rf_cal', 'fota', 'device_info', 'user_data', 'fota_info')


class ProtocolError(RuntimeError):
    pass


def checksum(data):
    return (~sum(data)) & 255


def frame(command, option_a=0, option_b=0, payload=b''):
    """Vendor Newrafc.generate: little endian options, complemented byte sums."""
    if not 0 <= command <= 255 or not 0 <= option_a <= 0xffffffff or not 0 <= option_b <= 0xffff:
        raise ValueError('Frame option out of range')
    size = 16 + len(payload)
    if size > 4112:
        raise ValueError('Frame exceeds vendor 4096-byte transfer limit')
    data = bytearray(struct.pack('<BBHIHBB', 1, command, size, option_a, option_b, 0, 0))
    data[11] = checksum(data[1:12])
    data += b'\x02' + payload + b'\x03\x00\x04'
    data[-2] = checksum(data)
    return bytes(data)


def decode_frame(data):
    if len(data) < 16 or len(data) != struct.unpack_from('<H', data, 2)[0]:
        raise ProtocolError('Invalid response size')
    if data[0] != 1 or data[12] != 2 or data[-3] != 3 or data[-1] != 4:
        raise ProtocolError('Invalid response framing')
    if checksum(data[1:12]) or checksum(data):
        raise ProtocolError('Response checksum mismatch')
    return data[1], struct.unpack_from('<I', data, 4)[0], struct.unpack_from('<H', data, 8)[0], bytes(data[13:-3])


def check_identity(port):
    if not str(port).startswith('/dev/serial/by-id/'):
        raise ValueError('SX port must be a persistent /dev/serial/by-id path')
    result = subprocess.run(['udevadm', 'info', '--query=property', '--name', str(port)],
                            check=True, text=True, capture_output=True)
    props = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
    if tuple(props.get(key) for key in ('ID_VENDOR_ID', 'ID_MODEL_ID', 'ID_SERIAL_SHORT')) != ('10c4', 'ea60', SX_SERIAL):
        raise ValueError('Port does not identify the confirmed SX CP2102N serial')
    return {'port': str(port), 'node': str(Path(port).resolve()),
            'usec_initialized': props.get('USEC_INITIALIZED'), 'devpath': props.get('DEVPATH'),
            'id_path': props.get('ID_PATH'), 'serial': SX_SERIAL,
            'kernel_boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip()}


class Serial:
    def __init__(self, port, baud=115200):
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.received = 0
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.set_baud(baud)
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except BaseException:
            os.close(self.fd)
            raise

    def set_baud(self, baud):
        if baud not in (115200, 460800):
            raise ValueError('Unsupported baud rate')
        settings = termios.tcgetattr(self.fd)
        settings[0] = settings[1] = settings[3] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[4] = settings[5] = getattr(termios, 'B' + str(baud))
        settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, settings)

    def send(self, data, timeout=5):
        deadline = time.monotonic() + timeout
        remaining = memoryview(data)
        while remaining:
            wait = deadline - time.monotonic()
            if wait <= 0 or not select.select([], [self.fd], [], wait)[1]:
                raise ProtocolError('SX transmit timeout')
            count = os.write(self.fd, remaining)
            if count <= 0:
                raise ProtocolError('SX transmit made no progress')
            remaining = remaining[count:]

    def receive(self, timeout=2):
        deadline = time.monotonic() + timeout
        data = bytearray()
        scanned = 0
        while True:
            # Initial console bytes are discarded without printing their contents.
            while data:
                if data[0] != 1:
                    del data[0]
                    continue
                if len(data) < 13:
                    break
                size = struct.unpack_from('<H', data, 2)[0]
                if not 16 <= size <= 4112 or data[12] != 2 or checksum(data[1:12]):
                    del data[0]
                    continue
                if len(data) < size:
                    break
                return decode_frame(data[:size])
            wait = deadline - time.monotonic()
            if wait <= 0 or not select.select([self.fd], [], [], wait)[0]:
                raise ProtocolError('No valid vendor response before timeout')
            chunk = os.read(self.fd, 4096)
            if not chunk:
                raise ProtocolError('SX disconnected')
            self.received += len(chunk)
            scanned += len(chunk)
            if scanned > 8192:
                raise ProtocolError('Response exceeded bounded input budget')
            data.extend(chunk)

    def close(self):
        os.close(self.fd)


def probe(serial):
    serial.send(frame(0x21))
    command, address, size, payload = serial.receive()
    if (command, address, size, payload) != (0x22, 0, 0, b''):
        raise ProtocolError('Response was not the expected vendor PONG')
    return {'event': 'sx_rom_probe', 'result': 'PASS', 'baud': 115200,
            'received_bytes': serial.received, 'ram_loaded': False, 'flash_write_commands': 0}


class ReadPolicy:
    """Only this object is allowed to create requests for the RAM read action.

    No user-supplied command or arbitrary address reaches the wire. Boot writes
    must exactly match the pinned vendor image, in order. After RUN, STORE_MEM
    may change only the 4-byte SYS_CONFIG RAM pointer to an approved read offset.
    """
    def __init__(self, boot):
        if len(boot) != BOOT_SIZE or hashlib.sha256(boot).hexdigest() != BOOT_SHA256:
            raise ValueError('The supplied 2nd bootloader size/SHA256 does not match the audited image')
        self.boot = boot
        self.uploaded = 0
        self.running = False
        self.allowed_offsets = set()

    def request(self, command, address=0, size=0, payload=b''):
        if command == 0x21 and (address, size, payload) == (0, 0, b''):
            return frame(command)
        if command == 0x23:
            if not self.running:
                end = self.uploaded + len(payload)
                allowed = (address == BOOT_ADDRESS + self.uploaded and 0 < len(payload) <= 4096
                           and size == len(payload) and end <= len(self.boot)
                           and payload == self.boot[self.uploaded:end])
                if allowed:
                    self.uploaded = end
                    return frame(command, address, size, payload)
            elif address == SYS_CONFIG_POINTER and size == 4 and len(payload) == 4:
                if struct.unpack('<I', payload)[0] in self.allowed_offsets:
                    return frame(command, address, size, payload)
        elif command == 0x2b and not self.running and self.uploaded == len(self.boot):
            if (address, size, payload) == (BOOT_ADDRESS, 0, b''):
                self.running = True
                return frame(command, address)
        elif self.running and command in (0x54, 0x58):
            if (address, size, payload) == (0, 0, b''):
                return frame(command)
        # 0x95 is deliberately absent: it hung the actual loader on 2026-09-09.
        # No off-line protocol model is evidence that its target implementation works.
        raise ValueError('Request rejected by SX RAM read command/address allowlist')


class Reader:
    def __init__(self, serial, policy):
        self.serial = serial
        self.policy = policy

    def exchange(self, command, response, address=0, size=0, payload=b'', response_size=0):
        self.serial.send(self.policy.request(command, address, size, payload))
        got, got_address, got_size, body = self.serial.receive(timeout=5)
        if got != response or len(body) != response_size:
            raise ProtocolError('Unexpected command or payload length in SX response')
        if command == 0x23 and (got_address, got_size) != (address, size):
            raise ProtocolError('RAM write acknowledgement address/length mismatch')
        if command in (0x21, 0x54, 0x58) and (got_address, got_size) != (0, 0):
            raise ProtocolError('Unexpected response options')
        return body

    def start(self):
        self.exchange(0x21, 0x22)
        for offset in range(0, len(self.policy.boot), 4096):
            chunk = self.policy.boot[offset:offset + 4096]
            self.exchange(0x23, 0x24, BOOT_ADDRESS + offset, len(chunk), chunk)
        self.serial.send(self.policy.request(0x2b, BOOT_ADDRESS))
        # Vendor cmd_run has no response and waits one second before changing baud.
        time.sleep(1)
        self.serial.set_baud(460800)
        self.exchange(0x21, 0x22)

    def memory_map(self):
        return self.exchange(0x58, 0x59, response_size=56)

    def resume(self, expected_map):
        # Resume is an explicit separate action; no RAM upload or RUN is possible.
        self.policy.running = True
        self.exchange(0x21, 0x22)
        if self.memory_map() != expected_map:
            raise ProtocolError('Live RAM map differs from the saved session; no pointer writes sent')

    def set_pointer(self, offset):
        self.exchange(0x23, 0x24, SYS_CONFIG_POINTER, 4, struct.pack('<I', offset))

    def read44(self, offset):
        self.set_pointer(offset)
        return self.exchange(0x54, 0x55, response_size=44)

def factory_regions(memory_map, flash_size):
    if flash_size not in FLASH_SIZES or len(memory_map) != 56:
        raise ValueError('Unsupported declared flash size or map size')
    fields = struct.unpack('<14I', memory_map)
    half = flash_size // 2 if flash_size > 0x100000 else 0x100000
    regions = [('memory_map_sector', 0xf000, 4096),
               ('sys_config_sector', fields[8], 4096),
               ('rf_cal_sector', fields[9], 4096),
               ('device_info_sector', fields[11], 4096),
               ('legacy_mac_sector', half - 0x2000, 4096)]
    selected, skipped, aliases = [], [], []
    seen = {}
    for name, offset, length in regions:
        if offset < 0xf000 or offset % 4096:
            raise ValueError('Factory sector map is malformed')
        if offset + length > flash_size:
            skipped.append({'name': name, 'offset': offset, 'size': length,
                            'result': 'SKIPPED', 'reason': 'outside loader-reported capacity'})
        elif (offset, length) in seen:
            aliases.append({'name': name, 'same_bytes_as': seen[(offset, length)], 'offset': offset})
        else:
            selected.append((name, offset, length))
            seen[(offset, length)] = name
    return selected, skipped, aliases


def read_offsets(offset, length):
    """The target always reads 44 bytes; overlap at the end instead of overread."""
    if length < 44:
        raise ValueError('Read range must contain a complete 44-byte target read')
    cursor = offset
    while cursor < offset + length:
        start = min(cursor, offset + length - 44)
        skip = cursor - start
        yield start, skip
        cursor = start + 44


def collect_region(reader, offset, length):
    data = bytearray()
    for start, skip in read_offsets(offset, length):
        data.extend(reader.read44(start)[skip:])
    if len(data) != length:
        raise ProtocolError('Factory sector byte count mismatch')
    return bytes(data)


def read_geometry(reader, original_map):
    """Read only the loader's fixed sf_info RAM, then restore its original map.

    The vendor flash callback adds 0x11000000 using 32-bit arithmetic. This one
    audited offset addresses RAM rather than flash; arbitrary wrapped addresses
    are never exposed. The remaining28 bytes are RX scratch and are discarded.
    """
    original = struct.unpack_from('<I', original_map, 32)[0]
    reader.policy.allowed_offsets.update((original, GEOMETRY_OFFSET))
    try:
        data = reader.read44(GEOMETRY_OFFSET)[:16]
    finally:
        reader.set_pointer(original)
        if reader.memory_map() != original_map:
            raise ProtocolError('RAM map restoration after geometry read was not confirmed')
        reader.policy.allowed_offsets.discard(GEOMETRY_OFFSET)
    block, sector, page, capacity = struct.unpack('<4I', data)
    if (block, sector, page) != (65536, 4096, 256) or capacity not in FLASH_SIZES:
        raise ProtocolError('Loader returned unexpected flash geometry; no sector reads allowed')
    return {'block_size': block, 'sector_size': sector, 'page_size': page, 'total_size': capacity}, data


def read_jedec_metadata(reader, original_map):
    """Optional fixed RAM stack observation; never changes allowed flash bounds."""
    original = struct.unpack_from('<I', original_map, 32)[0]
    reader.policy.allowed_offsets.update((original, JEDEC_STACK_OFFSET))
    data = None
    try:
        data = reader.read44(JEDEC_STACK_OFFSET)
    except ProtocolError:
        # A failed optional read may be ignored only if the map can be restored
        # and positively queried below. No invalid ID is treated as capacity proof.
        pass
    finally:
        reader.set_pointer(original)
        if reader.memory_map() != original_map:
            raise ProtocolError('RAM map restoration after optional JEDEC metadata was not confirmed')
        reader.policy.allowed_offsets.discard(JEDEC_STACK_OFFSET)
    if data is None:
        return {'status': 'UNKNOWN', 'reason': 'optional metadata read failed'}, None
    saved_r4, saved_r5 = struct.unpack_from('<II', data, 12)
    saved_lr = struct.unpack_from('<I', data, 28)[0]
    if saved_r4 != 0 or saved_lr != 0x1048a22f or not 0 < saved_r5 < 0xffffff:
        return {'status': 'UNKNOWN', 'reason': 'fixed loader stack sentinels or ID did not validate'}, data
    return {'status': 'READ', 'raw_jedec_id': f'0x{saved_r5:06x}',
            'source': 'fixed saved-r5 stack metadata; capacity not inferred'}, data


def private_directory(root):
    root = Path(root).expanduser().resolve()
    parent = root
    while not parent.exists():
        parent = parent.parent
    in_git = subprocess.run(['git', '-C', str(parent), 'rev-parse', '--show-toplevel'],
                            capture_output=True).returncode == 0
    if in_git:
        raise ValueError('Raw factory backup directory must be outside every Git worktree')
    root.mkdir(mode=0o700, parents=True, exist_ok=True)
    if root.stat().st_uid != os.getuid() or root.stat().st_mode & 0o077:
        raise ValueError('Private backup root must be owned by this user with mode0700')
    directory = root / (time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()))
    directory.mkdir(mode=0o700)
    return directory


def private_write(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'wb') as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def checkpoint_manifest(directory, manifest):
    """Atomically retain progress before sending another target request."""
    with tempfile.NamedTemporaryFile(dir=directory, prefix='.manifest-', delete=False) as output:
        temporary = Path(output.name)
        try:
            os.fchmod(output.fileno(), 0o600)
            output.write((json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode())
            output.flush()
            os.fsync(output.fileno())
            os.replace(temporary, directory / 'manifest.json')
        finally:
            temporary.unlink(missing_ok=True)


def load_resume(path, identity):
    path = Path(path).expanduser().resolve()
    info = path.stat()
    if info.st_uid != os.getuid() or info.st_mode & 0o077 or info.st_size > 65536:
        raise ValueError('Resume manifest must be a private0600 file owned by this user')
    manifest = json.loads(path.read_text())
    good_info = manifest.get('event') == 'sx_ram_info' and manifest.get('result') == 'PASS'
    good_factory = (manifest.get('event') == 'sx_factory_read'
                    and manifest.get('acquisition_result') == 'PASS'
                    and manifest.get('result') in ('PASS', 'PARTIAL'))
    if not (good_info or good_factory) or manifest.get('pointer_restored') is not True or manifest.get('boot_sha256') != BOOT_SHA256:
        raise ValueError('Resume requires successful audited acquisition with restored pointer')
    saved_map = (path.parent / 'ram_memory_map.bin').read_bytes()
    saved_geometry = (path.parent / 'loader_geometry.bin').read_bytes()
    if len(saved_map) != 56 or (manifest.get('map_sha256') is not None
                               and hashlib.sha256(saved_map).hexdigest() != manifest['map_sha256']):
        raise ValueError('Saved RAM map length/hash mismatch')
    if len(saved_geometry) != 16 or struct.unpack('<4I', saved_geometry) != tuple(
            manifest['geometry'][key] for key in ('block_size', 'sector_size', 'page_size', 'total_size')):
        raise ValueError('Saved loader geometry mismatch')
    saved_identity = manifest.get('usb_identity')
    if saved_identity is not None:
        if saved_identity != identity:
            raise ValueError('USB enumeration changed since the saved RAM session')
    else:
        # Compatibility with the first real RAM-info manifest, written before
        # identity recording existed. Refuse any enumeration newer than that file.
        initialized = identity.get('usec_initialized')
        if not initialized or not initialized.isdigit():
            raise ValueError('udev initialization timestamp unavailable for legacy manifest resume')
        manifest_monotonic = time.monotonic() - (time.time() - info.st_mtime)
        if manifest_monotonic < 0 or int(initialized) / 1e6 > manifest_monotonic:
            raise ValueError('USB enumeration is newer than the saved RAM-info manifest')
    return {'manifest': str(path), 'map': saved_map, 'geometry': manifest['geometry'],
            'usb_identity': identity, 'record': manifest}


def resume_legacy_marker(serial, boot, directory, resume):
    """One exact potentially unmapped vendor address; never widens this read API."""
    entries = [entry for entry in resume['record'].get('files', [])
               if entry.get('name') == 'sys_config_sector.bin']
    if len(entries) != 1 or entries[0].get('repeat_match') is not True:
        raise ValueError('Legacy marker check requires the twice-matched physical SYS sector backup')
    entry = entries[0]
    reference = (Path(resume['manifest']).parent / 'sys_config_sector.bin').read_bytes()
    if len(reference) != 4096 or hashlib.sha256(reference).hexdigest() != entry.get('sha256'):
        raise ValueError('Physical SYS sector backup length/hash mismatch')
    policy = ReadPolicy(boot)
    reader = Reader(serial, policy)
    manifest = {'event': 'sx_legacy_marker', 'result': 'FAIL', 'flash_write_commands': 0,
                'resumed_from': resume['manifest'], 'directory': str(directory),
                'pointer_restored': False, 'legacy_marker_nrcnrt': None, 'matches_physical_sys': None}
    original_map = None
    checkpoint_manifest(directory, manifest)
    try:
        reader.resume(resume['map'])
        original_map = resume['map']
        original_offset = struct.unpack_from('<I', original_map, 32)[0]
        policy.allowed_offsets = {original_offset, LEGACY_MAC_OFFSET}
        raw = reader.read44(LEGACY_MAC_OFFSET)
        private_write(directory / 'legacy_literal_ffffe000.bin', raw)
        manifest['legacy_marker_nrcnrt'] = raw[8:14] == b'nrcnrt'
        manifest['matches_physical_sys'] = raw == reference[:44]
        manifest['result'] = 'PASS'
        checkpoint_manifest(directory, manifest)
    finally:
        if original_map is not None:
            try:
                reader.set_pointer(struct.unpack_from('<I', original_map, 32)[0])
                if reader.memory_map() != original_map:
                    raise ProtocolError('Legacy marker RAM map restoration mismatch')
                manifest['pointer_restored'] = True
            except (OSError, ProtocolError, ValueError):
                manifest['result'] = 'FAIL'
                manifest['restore_error'] = 'RAM restoration unconfirmed; power-cycle before GUI or any other action'
        checkpoint_manifest(directory, manifest)
    if not manifest['pointer_restored']:
        raise ProtocolError('RAM restoration unconfirmed; power-cycle before GUI or any other action')
    return manifest


def read_factory(serial, boot, flash_size, directory, resume=None, identity=None):
    policy = ReadPolicy(boot)
    reader = Reader(serial, policy)
    manifest = {'event': 'sx_factory_read', 'result': 'FAIL', 'boot_sha256': BOOT_SHA256,
                'flash_size_declared': flash_size,
                'flash_size_source': 'loader JEDEC table; vendor may use a default for unknown IDs',
                'flash_write_commands': 0, 'ram_loaded': False, 'pointer_restored': False,
                'usb_identity': identity, 'resumed_from': resume['manifest'] if resume else None,
                'target_crc_status': 'NOT_TESTED',
                'target_crc_reason': '0x95 disabled after actual loader timeout; host CRC is not target validation',
                'acquisition_result': 'IN_PROGRESS',
                'directory': str(directory), 'files': []}
    original_map = None
    checkpoint_manifest(directory, manifest)
    try:
        if resume:
            reader.resume(resume['map'])
        else:
            reader.start()
        manifest['ram_loaded'] = True
        original_map = reader.memory_map()
        private_write(directory / 'ram_memory_map.bin', original_map)
        geometry, raw_geometry = read_geometry(reader, original_map)
        private_write(directory / 'loader_geometry.bin', raw_geometry)
        manifest['geometry'] = geometry
        if resume and geometry != resume['geometry']:
            raise ProtocolError('Live loader geometry differs from saved session; factory reads aborted')
        if flash_size is not None and flash_size != geometry['total_size']:
            raise ProtocolError('Explicit flash size differs from the loader geometry')
        flash_size = geometry['total_size']
        regions, skipped, aliases = factory_regions(original_map, flash_size)
        manifest['skipped_regions'] = skipped
        manifest['region_aliases'] = aliases
        manifest['coverage'] = 'confirmed in-bound factory sectors only; not a full flash backup'
        original_offset = struct.unpack_from('<I', original_map, 32)[0]
        policy.allowed_offsets = {original_offset}
        for _, offset, length in regions:
            policy.allowed_offsets.update(start for start, _ in read_offsets(offset, length))
        for name, offset, length in regions:
            # Two independent sweeps must match; a timeout/corruption is never padded.
            data = collect_region(reader, offset, length)
            second = collect_region(reader, offset, length)
            if data != second:
                raise ProtocolError('Repeated factory sector reads disagree')
            path = directory / (name + '.bin')
            # Preserve both-matched bytes before any further target operation.
            private_write(path, data)
            manifest['files'].append({'name': path.name, 'offset': offset, 'size': len(data),
                                      'sha256': hashlib.sha256(data).hexdigest(),
                                      'host_crc32': f'{zlib.crc32(data):08x}', 'repeat_match': True,
                                      'target_crc_status': 'NOT_TESTED'})
            manifest['result'] = 'PARTIAL'
            checkpoint_manifest(directory, manifest)
        # Optional diagnostics happen only after acquired sector bytes are durable.
        jedec, raw_jedec = read_jedec_metadata(reader, original_map)
        manifest['jedec_metadata'] = jedec
        if raw_jedec is not None:
            private_write(directory / 'loader_jedec_stack.bin', raw_jedec)
        manifest['acquisition_result'] = 'PASS'
    except BaseException:
        manifest['acquisition_result'] = 'FAIL'
        raise
    finally:
        manifest['ram_loaded'] = policy.running
        if original_map is not None:
            try:
                original_offset = struct.unpack_from('<I', original_map, 32)[0]
                policy.allowed_offsets.add(original_offset)
                reader.set_pointer(original_offset)
                if reader.memory_map() != original_map:
                    raise ProtocolError('RAM map did not match after pointer restoration')
                manifest['pointer_restored'] = True
            except (OSError, ProtocolError, ValueError):
                manifest['result'] = 'FAIL'
                manifest['restore_error'] = 'RAM map restoration was not confirmed; power-cycle before any other tool'
        checkpoint_manifest(directory, manifest)
    if not manifest['pointer_restored']:
        raise ProtocolError('RAM map restoration was not confirmed; power-cycle before any other tool')
    return manifest


def read_info(serial, boot, directory, identity=None):
    policy = ReadPolicy(boot)
    reader = Reader(serial, policy)
    result = {'event': 'sx_ram_info', 'result': 'FAIL', 'boot_sha256': BOOT_SHA256,
              'flash_write_commands': 0, 'pointer_restored': False,
              'usb_identity': identity,
              'flash_size_source': 'loader JEDEC table; vendor may use a default for unknown IDs',
              'directory': str(directory)}
    try:
        reader.start()
        memory_map = reader.memory_map()
        private_write(directory / 'ram_memory_map.bin', memory_map)
        geometry, raw_geometry = read_geometry(reader, memory_map)
        private_write(directory / 'loader_geometry.bin', raw_geometry)
        result['pointer_restored'] = True
        result['geometry'] = geometry
        result['flash_size'] = geometry['total_size']
        result['map'] = {name: f'0x{value:08x}' for name, value in
                         zip(MAP_FIELDS, struct.unpack('<14I', memory_map))}
        result['map_sha256'] = hashlib.sha256(memory_map).hexdigest()
        result['result'] = 'PASS'
        return result
    finally:
        result['ram_loaded'] = policy.running
        private_write(directory / 'manifest.json', (json.dumps(result, indent=2, sort_keys=True) + '\n').encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['probe', 'ram-info', 'ram-read-factory', 'resume-read-factory', 'resume-legacy-marker'], nargs='?', default='probe')
    parser.add_argument('--port', default=SX_PORT)
    parser.add_argument('--boot', type=Path, default=BOOT_PATH)
    parser.add_argument('--flash-size', type=lambda value: int(value, 0), choices=FLASH_SIZES,
                        help='Optional assertion: require loader geometry to equal this capacity')
    parser.add_argument('--output-root', type=Path, default=PRIVATE_ROOT)
    parser.add_argument('--resume-manifest', type=Path,
                        help='Successful private RAM-info manifest from this USB enumeration')
    args = parser.parse_args()
    serial = None
    try:
        boot = None
        directory = None
        resume = None
        if args.action != 'probe':
            boot = args.boot.read_bytes()
            ReadPolicy(boot)  # Reject an unpinned image before opening serial.
            directory = private_directory(args.output_root)
        identity = check_identity(args.port)
        if args.action in ('resume-read-factory', 'resume-legacy-marker'):
            if args.resume_manifest is None:
                parser.error('resume action requires --resume-manifest')
            resume = load_resume(args.resume_manifest, identity)
        elif args.resume_manifest is not None:
            parser.error('--resume-manifest is only valid for a resume action')
        serial = Serial(args.port, baud=460800 if resume else 115200)
        if check_identity(args.port) != identity:
            raise ValueError('USB enumeration changed while opening serial')
        if args.action == 'probe':
            result = probe(serial)
        elif args.action == 'ram-info':
            result = read_info(serial, boot, directory, identity)
        elif args.action == 'resume-legacy-marker':
            result = resume_legacy_marker(serial, boot, directory, resume)
        else:
            result = read_factory(serial, boot, args.flash_size, directory, resume, identity)
        print(json.dumps(result, sort_keys=True))
        return 0
    except (OSError, ValueError, ProtocolError, subprocess.CalledProcessError) as error:
        print(json.dumps({'event': {'probe': 'sx_rom_probe', 'ram-info': 'sx_ram_info',
                                   'ram-read-factory': 'sx_factory_read',
                                   'resume-read-factory': 'sx_factory_read',
                                   'resume-legacy-marker': 'sx_legacy_marker'}[args.action],
                          'result': 'FAIL', 'error': str(error),
                          'received_bytes': serial.received if serial else 0,
                          'ram_loaded': False if args.action == 'probe' else 'see private manifest',
                          'directory': str(directory) if directory else None,
                          'flash_write_commands': 0}), file=sys.stderr)
        return 1
    finally:
        if serial is not None:
            serial.close()


if __name__ == '__main__':
    raise SystemExit(main())
