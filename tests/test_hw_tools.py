#!/usr/bin/env python3
"""Small host-only guard: incomplete/error console responses never become PASS."""
import importlib.util
import contextlib
import io
import os
import pathlib
import pty
import socket
import shutil
import subprocess
import struct
import sys
import tarfile
import tempfile
import tty
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('hw_console', ROOT / 'tools/hw_console.py')
hw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hw)
assert hw.event('boot text') is None
assert hw.event('prefix {"event":"probe","error":0}') == {'event': 'probe', 'error': 0}
diag = dict(event='diag', faulted=0, chip_id=0x7292, signature='NRC-HSPI', cs_gpio=6,
            clock_hz=5000000, rx_slot_num=4, tx_slot_num=4, rx_slot_size=512, tx_slot_size=512)
hw.validate_probe(diag, 'ce0')
for invalid in ({}, dict(diag, faulted=1), dict(diag, signature='INVALID'), dict(diag, cs_gpio=5), dict(diag, tx_slot_num=0)):
    try:
        hw.validate_probe(invalid, 'ce0')
        raise AssertionError('Unproven probe diagnostics accepted')
    except ValueError:
        pass
hw.passed({'event': 'loopback', 'error': 0, 'requested': 1000, 'passed': 1000, 'bytes': 1518}, 'loopback', 1000, 1518)
hw.passed({'event': 'ping', 'error': 0, 'sent': 5, 'received': 5}, 'ping')
try:
    hw.passed({'event': 'ping', 'error': 0, 'sent': 5, 'received': 4}, 'ping')
    raise AssertionError('Incomplete ping accepted')
except ValueError:
    pass
for response in ({'event': 'probe'}, {'event': 'probe', 'error': 1}, {'event': 'diag', 'error': 0}):
    try:
        hw.passed(response, 'probe')
        raise AssertionError('Invalid probe accepted')
    except ValueError:
        pass
try:
    hw.passed({'event': 'loopback', 'error': 0, 'requested': 1000, 'passed': 999, 'bytes': 1518}, 'loopback', 1000, 1518)
    raise AssertionError('Incomplete loopback accepted')
except ValueError:
    pass
# Coalesced firmware output must not leave a stale diag for the next command.
master, slave = pty.openpty()
try:
    tty.setraw(slave)
    console = hw.Console.__new__(hw.Console)
    console.fd, console.pending = slave, b''
    os.write(master, b'{"event":"probe","error":0}\n{"event":"diag","old":1}\n')
    with contextlib.redirect_stdout(io.StringIO()):
        assert console.read(.1, 'probe')['error'] == 0
        os.write(master, b'{"event":"diag","old":0}\n')
        assert console.read(.1, 'diag')['old'] == 0
finally:
    os.close(master)
    os.close(slave)
# Deterministic UDP validates CRC AND byte pattern, and separates duty from loss.
sys.path.insert(0, str(ROOT / 'tools'))
import test_udp
payload = bytes((7 * 37 + index * 73) & 255 for index in range(992))
packet = struct.pack('!4sBBH6I', b'NRTU', 1, 0, 32, 42, 7, 100000, 100, 992, zlib.crc32(payload)) + payload
assert test_udp.decode(packet) == (42, 7, 100000, 100)
changed = bytearray(packet)
changed[-1] ^= 1
struct.pack_into('!I', changed, 28, zlib.crc32(changed[32:]))
for broken in (packet[:-1], bytes(changed), packet[:4] + b'\2' + packet[5:]):
    try:
        test_udp.decode(broken)
        raise AssertionError('Malformed or corrupted UDP accepted')
    except ValueError:
        pass
sender = dict(event='udp', error=0, run_id=42, sent=3, offered=3, bytes=3072,
              duty_pauses=1, duty_blocked_ms=300, send_errors=0, elapsed_ms=1000,
              bps=100000, seconds=1, duty_measurement='sampled_target_availability')
complete = test_udp.summarize(sender, {42: {0, 1, 2}}, 0, 1, 1, 100000, 1)
assert complete['result'] == 'PASS' and complete['lost'] == 0 and not complete['requested_rate_met']
assert complete['duty_blocked_ms'] == 300 and complete['duplicates'] == 1
incomplete = test_udp.summarize(sender, {42: {0, 2}}, 0, 0, 0, 100000, 1)
assert incomplete['result'] == 'FAIL' and incomplete['lost'] == 1 and incomplete['duty_blocked_ms'] == 300
assert test_udp.summarize(sender, {42: {0, 1, 2}}, 1, 0, 0, 100000, 1)['result'] == 'FAIL'
failed_sender = test_udp.summarize(dict(sender, error=-1, send_errors=1), {42: {0, 1, 2}}, 0, 0, 0, 100000, 1)
assert failed_sender['result'] == 'FAIL' and failed_sender['duty_blocked_ms'] == 300
# Exercise the concurrent receiver on localhost; this is explicitly a host test.
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receive, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as transmit:
    receive.bind(('127.0.0.1', 0))

    class FakeConsole:
        def command(self, text, expected, timeout):
            assert text == 'udp 100000 1' and expected == 'udp'
            for sequence in (0, 2, 2, 1):
                data = bytes((sequence * 37 + index * 73) & 255 for index in range(992))
                header = struct.pack('!4sBBH6I', b'NRTU', 1, 0, 32, 42, sequence, 100000, 10, 992, zlib.crc32(data))
                transmit.sendto(header + data, receive.getsockname())
            return sender

    actual = test_udp.receive_run(FakeConsole(), receive, '127.0.0.1', 100000, 1, .1)
    assert actual['result'] == 'PASS' and actual['received'] == 3
    assert actual['duplicates'] == 1 and actual['out_of_order'] == 1
# A caller-selected external LOG_DIR must appear in the evidence archive.
with tempfile.TemporaryDirectory() as temporary:
    base = pathlib.Path(temporary)
    project, outside = base / 'project', base / 'outside'
    (project / 'tools').mkdir(parents=True)
    outside.mkdir()
    (outside / 'run.log').write_text('host-only evidence\n')
    collector = project / 'tools/collect_logs.sh'
    shutil.copy2(ROOT / 'tools/collect_logs.sh', collector)
    archive = base / 'evidence.tar.gz'
    subprocess.run([str(collector), str(archive)], env=dict(os.environ, LOG_DIR=str(outside)),
                   capture_output=True, text=True, check=True)
    with tarfile.open(archive) as evidence:
        member = str(outside).lstrip('/') + '/run.log'
        assert evidence.extractfile(member).read() == b'host-only evidence\n'
subprocess.run(['node', '-e', '''
const assert=require('node:assert/strict');
const {matchFrame}=require('./tools/capture_video.cjs');
const frame={codec:1,key:true,data:Buffer.from([0,1,2,255])};
const clip={frames:[frame]};
assert.equal(matchFrame({...frame,data:Buffer.from([0,1,2,255])},clip),0);
assert.equal(matchFrame({...frame,data:Buffer.from([0,1,2,254])},clip),-1);
assert.equal(matchFrame({...frame,key:false},clip),-1);
'''], cwd=ROOT, check=True)
print('PASS: host automation rejects missing/error/incomplete responses and corrupted UDP/video; no hardware exercised.')
