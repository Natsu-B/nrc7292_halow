#!/usr/bin/env python3
"""Offline protocol/policy/collection tests. Never opens a serial device."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sx_read_factory', ROOT / 'tools/sx_read_factory.py')
sx = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sx)


def rejected(fn):
    try:
        fn()
    except (ValueError, sx.ProtocolError):
        return
    raise AssertionError('Forbidden or malformed input was accepted')


class FakeSerial:
    def __init__(self, fail_second_sector=False):
        self.map = struct.pack('<14I', 0, 0x10000, 0xf5000, 0xf6000, 0xfa000, 0xfb000,
                               0xfc000, 0xfd000, 0xfe000, 0xff000, 0x80000, 0xf9000, 0xf7000, 0xf8000)
        self.original_map = self.map
        self.flash = bytes((i * 73 + i // 251) & 255 for i in range(0x100000))
        self.commands = []
        self.response = None
        self.received = 0
        self.fail_second_sector = fail_second_sector

    def send(self, packet):
        command, address, size, payload = sx.decode_frame(packet)
        self.commands.append((command, address, size, payload))
        if command == 0x21:
            self.response = (0x22, 0, 0, b'')
        elif command == 0x23:
            if address == sx.SYS_CONFIG_POINTER:
                assert len(payload) == 4
                self.map = self.map[:32] + payload + self.map[36:]
            else:
                assert sx.BOOT_ADDRESS <= address < sx.BOOT_ADDRESS + sx.BOOT_SIZE
            self.response = (0x24, address, size, b'')
        elif command == 0x2b:
            assert address == sx.BOOT_ADDRESS
            self.response = None
        elif command == 0x58:
            self.response = (0x59, 0, 0, self.map)
        elif command == 0x54:
            offset = struct.unpack_from('<I', self.map, 32)[0]
            if offset == sx.GEOMETRY_OFFSET:
                data = struct.pack('<4I', 65536, 4096, 256, len(self.flash)) + bytes(28)
            elif offset == sx.JEDEC_STACK_OFFSET:
                data = bytearray(44)
                struct.pack_into('<I', data, 16, 0xbf2654)
                struct.pack_into('<I', data, 28, 0x1048a22f)
                data = bytes(data)
            else:
                if self.fail_second_sector and offset == 0xfe000:
                    raise sx.ProtocolError('Synthetic later-sector failure')
                assert offset + 44 <= len(self.flash)
                data = self.flash[offset:offset + 44]
            self.response = (0x55, 0, 0, data)
        else:
            raise AssertionError(f'Unapproved command {command:#x}')

    def receive(self, timeout=2):
        assert self.response is not None
        result, self.response = self.response, None
        return result

    def set_baud(self, baud):
        assert baud == 460800


class FactoryReadTests(unittest.TestCase):
    def setUp(self):
        # Deterministic mock firmware exercises pin validation without redistributing
        # the vendor binary. Production constants remain separately asserted below.
        self.boot = bytes((i * 37) & 255 for i in range(sx.BOOT_SIZE))
        self.hash_patch = patch.object(sx, 'BOOT_SHA256', hashlib.sha256(self.boot).hexdigest())
        self.hash_patch.start()
        self.addCleanup(self.hash_patch.stop)

    def loaded_policy(self):
        policy = sx.ReadPolicy(self.boot)
        for offset in range(0, len(self.boot), 4096):
            data = self.boot[offset:offset + 4096]
            policy.request(0x23, sx.BOOT_ADDRESS + offset, len(data), data)
        policy.request(0x2b, sx.BOOT_ADDRESS)
        return policy

    def test_vendor_fixed_vectors_and_corruption(self):
        self.assertEqual(sx.frame(0x21).hex(), '0121100000000000000000ce0203f604')
        self.assertEqual(sx.frame(0x22).hex(), '0122100000000000000000cd0203f604')
        for payload in (b'', bytes(range(256)), bytes(4096)):
            packet = sx.frame(0x24, 0x10488000, len(payload), payload)
            self.assertEqual(sx.decode_frame(packet), (0x24, 0x10488000, len(payload), payload))
            for index in (0, 2, 4, 11, 12, len(packet) - 2, len(packet) - 1):
                bad = bytearray(packet)
                bad[index] ^= 1
                rejected(lambda: sx.decode_frame(bad))

    def test_boot_and_ram_address_allowlist(self):
        rejected(lambda: sx.ReadPolicy(self.boot[:-1]))
        rejected(lambda: sx.ReadPolicy(bytes(sx.BOOT_SIZE)))
        policy = sx.ReadPolicy(self.boot)
        rejected(lambda: policy.request(0x2b, sx.BOOT_ADDRESS))
        rejected(lambda: policy.request(0x23, sx.BOOT_ADDRESS + 1, 4, self.boot[:4]))
        rejected(lambda: policy.request(0x23, sx.BOOT_ADDRESS, 4, b'wrong'))
        policy = self.loaded_policy()
        policy.allowed_offsets = {0xfe000}
        for cmd in range(256):
            if cmd not in (0x21, 0x23, 0x54, 0x58):
                rejected(lambda cmd=cmd: policy.request(cmd))
        for address in (0, sx.SYS_CONFIG_POINTER - 1, sx.SYS_CONFIG_POINTER + 1, sx.BOOT_ADDRESS):
            rejected(lambda address=address: policy.request(0x23, address, 4, struct.pack('<I', 0xfe000)))
        rejected(lambda: policy.request(0x23, sx.SYS_CONFIG_POINTER, 4, struct.pack('<I', 0)))
        rejected(lambda: policy.request(0x95, payload=struct.pack('<II', 0xfe000, 4097)))
        policy.request(0x23, sx.SYS_CONFIG_POINTER, 4, struct.pack('<I', 0xfe000))
        rejected(lambda: policy.request(0x95, payload=struct.pack('<II', 0xfe000, 4096)))

    def test_overlap_tail_does_not_overread(self):
        for offset in (0, 0xff000):
            chunks = list(sx.read_offsets(offset, 4096))
            self.assertEqual(chunks[-1], (offset + 4096 - 44, 40))
            self.assertTrue(all(start + 44 <= offset + 4096 for start, _ in chunks))
            self.assertEqual(sum(44 - skip for _, skip in chunks), 4096)

    def test_probe_only(self):
        serial = FakeSerial()
        self.assertEqual(sx.probe(serial)['result'], 'PASS')
        self.assertEqual([x[0] for x in serial.commands], [0x21])

    def test_boot_info_only_reads_fixed_geometry_and_restores(self):
        serial = FakeSerial()
        with tempfile.TemporaryDirectory() as temp, patch.object(sx.time, 'sleep'):
            result = sx.read_info(serial, self.boot, Path(temp))
            self.assertEqual(result['result'], 'PASS')
            self.assertEqual(result['flash_size'], 0x100000)
            self.assertTrue(result['pointer_restored'])
        self.assertEqual(serial.map, serial.original_map)
        pointers = [struct.unpack('<I', payload)[0] for cmd, address, _, payload in serial.commands
                    if cmd == 0x23 and address == sx.SYS_CONFIG_POINTER]
        self.assertEqual(pointers, [sx.GEOMETRY_OFFSET, 0xfe000])

    def test_complete_factory_backup_permissions_and_restoration(self):
        serial = FakeSerial()
        with tempfile.TemporaryDirectory() as temp, patch.object(sx.time, 'sleep'):
            result = sx.read_factory(serial, self.boot, 0x100000, Path(temp))
            self.assertEqual(result['result'], 'PARTIAL')
            self.assertEqual(result['acquisition_result'], 'PASS')
            self.assertTrue(result['pointer_restored'])
            self.assertEqual(len(result['files']), 4)
            self.assertEqual(result['region_aliases'][0]['name'], 'legacy_mac_sector')
            for entry in result['files']:
                path = Path(temp) / entry['name']
                self.assertEqual(path.stat().st_mode & 0o777, 0o600)
                self.assertEqual(path.read_bytes(), serial.flash[entry['offset']:entry['offset'] + entry['size']])
                self.assertEqual(entry['target_crc_status'], 'NOT_TESTED')
                self.assertNotIn('target_crc_match', entry)
            self.assertEqual(serial.map, serial.original_map)

    def test_later_failure_keeps_acquired_bytes_and_restores_pointer(self):
        serial = FakeSerial(fail_second_sector=True)
        with tempfile.TemporaryDirectory() as temp, patch.object(sx.time, 'sleep'):
            with self.assertRaises(sx.ProtocolError):
                sx.read_factory(serial, self.boot, 0x100000, Path(temp))
            manifest = json.loads((Path(temp) / 'manifest.json').read_text())
            self.assertEqual(manifest['result'], 'PARTIAL')
            self.assertEqual(manifest['acquisition_result'], 'FAIL')
            self.assertTrue(manifest['pointer_restored'])
            self.assertEqual(len(manifest['files']), 1)
            saved = Path(temp) / manifest['files'][0]['name']
            self.assertEqual(saved.read_bytes(), serial.flash[0xf000:0x10000])
            self.assertEqual(saved.stat().st_mode & 0o777, 0o600)
            self.assertEqual(manifest['target_crc_status'], 'NOT_TESTED')
        self.assertEqual(serial.map, serial.original_map)

    def test_invalid_optional_jedec_does_not_discard_backup(self):
        serial = FakeSerial()
        original_send = serial.send
        def invalid_stack(packet):
            original_send(packet)
            command, _, _, _ = sx.decode_frame(packet)
            if command == 0x54 and struct.unpack_from('<I', serial.map, 32)[0] == sx.JEDEC_STACK_OFFSET:
                serial.response = (0x55, 0, 0, bytes(44))
        serial.send = invalid_stack
        with tempfile.TemporaryDirectory() as temp, patch.object(sx.time, 'sleep'):
            result = sx.read_factory(serial, self.boot, None, Path(temp))
        self.assertEqual(result['acquisition_result'], 'PASS')
        self.assertEqual(result['jedec_metadata']['status'], 'UNKNOWN')
        self.assertTrue(result['pointer_restored'])

    def test_private_directory_rejects_git(self):
        rejected(lambda: sx.private_directory(ROOT / 'logs' / 'raw_factory'))

    def test_resume_skips_outside_capacity_and_does_not_load_or_run(self):
        serial = FakeSerial()
        fields = list(struct.unpack('<14I', serial.map))
        fields[11] = 0x1e5000
        serial.map = serial.original_map = struct.pack('<14I', *fields)
        resume = {'manifest': 'mock-private-manifest', 'map': serial.map,
                  'geometry': {'block_size': 65536, 'sector_size': 4096,
                               'page_size': 256, 'total_size': 0x100000}}
        with tempfile.TemporaryDirectory() as temp:
            result = sx.read_factory(serial, self.boot, None, Path(temp), resume)
        self.assertEqual(result['result'], 'PARTIAL')
        self.assertEqual(result['acquisition_result'], 'PASS')
        self.assertEqual(len(result['files']), 3)
        self.assertEqual(result['skipped_regions'][0]['name'], 'device_info_sector')
        self.assertEqual(serial.map, serial.original_map)
        self.assertFalse(any(command == 0x2b or command == 0x23 and address != sx.SYS_CONFIG_POINTER
                             for command, address, _, _ in serial.commands))

    def test_resume_changed_map_aborts_before_pointer_writes(self):
        serial = FakeSerial()
        resume = {'manifest': 'mock-private-manifest', 'map': bytes(56), 'geometry': {}}
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(sx.ProtocolError):
                sx.read_factory(serial, self.boot, None, Path(temp), resume)
        self.assertEqual([command for command, _, _, _ in serial.commands], [0x21, 0x58])

    def test_resume_manifest_binds_usb_enumeration(self):
        identity = {'serial': sx.SX_SERIAL, 'usec_initialized': '1', 'devpath': '/devices/mock'}
        with tempfile.TemporaryDirectory() as temp, patch.object(sx.time, 'sleep'):
            sx.read_info(FakeSerial(), self.boot, Path(temp), identity)
            path = Path(temp) / 'manifest.json'
            resumed = sx.load_resume(path, identity)
            self.assertEqual(resumed['usb_identity'], identity)
            rejected(lambda: sx.load_resume(path, dict(identity, devpath='/devices/replugged')))
            legacy = json.loads(path.read_text())
            del legacy['usb_identity']
            path.write_text(json.dumps(legacy))
            sx.load_resume(path, identity)
            newer = str(int((sx.time.monotonic() + 3600) * 1e6))
            rejected(lambda: sx.load_resume(path, dict(identity, usec_initialized=newer)))


if __name__ == '__main__':
    assert sx.BOOT_SHA256 == '33db5706c1be366c07908eb0093ad5a6c1254778149415bf16971bc12c9c4057'
    assert sx.BOOT_SIZE == 10680 and sx.BOOT_ADDRESS == 0x10488000
    unittest.main()
