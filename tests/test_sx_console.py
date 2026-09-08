#!/usr/bin/env python3
"""Passive capture survives device disappearance and keeps its total deadline."""
import os
import pathlib
import pty
import select
import subprocess
import sys
import tempfile
import time

tool = pathlib.Path(__file__).resolve().parents[1] / 'tools/sx_console.py'
with tempfile.TemporaryDirectory() as directory:
    port, log = pathlib.Path(directory) / 'same-by-id', pathlib.Path(directory) / 'boot.log'
    started = time.monotonic()
    child = subprocess.Popen([sys.executable, str(tool), '--reconnect', '--port', str(port),
                              '--seconds', '2', '--log', str(log)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    masters, slaves = [], []
    try:
        for index in range(2):
            master, slave = pty.openpty()
            masters.append(master)
            slaves.append(slave)
            port.symlink_to(os.ttyname(slave))
            deadline = time.monotonic() + .7
            while not log.exists() or log.read_bytes().count(b'baud=115200') <= index:
                assert time.monotonic() < deadline, 'device was not reopened'
                time.sleep(.01)
            os.write(master, f'boot-{index}\n'.encode())
            while f'boot-{index}'.encode() not in log.read_bytes():
                assert time.monotonic() < deadline, 'boot bytes lost'
                time.sleep(.01)
            assert not select.select([master], [], [], 0)[0], 'passive monitor transmitted bytes'
            port.unlink()
            os.close(master)
            masters.pop()
            time.sleep(.05)
        stdout, stderr = child.communicate(timeout=3)
        assert child.returncode == 0, stderr
        assert b'boot-0' in stdout and b'boot-1' in stdout
        assert 1.8 <= time.monotonic() - started < 3, 'deadline restarted after reconnect'
        assert b'# TX' not in log.read_bytes()
    finally:
        if child.poll() is None:
            child.kill()
            child.wait()
        for descriptor in masters + slaves:
            os.close(descriptor)
    invalid = subprocess.run([sys.executable, str(tool), '--reconnect', '--log', str(log), 'sx reset'], capture_output=True)
    assert invalid.returncode != 0 and b'passive only' in invalid.stderr
print('PASS: same by-id reconnect captures both boots without UART writes; total deadline stays bounded')
