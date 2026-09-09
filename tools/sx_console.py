#!/usr/bin/env python3
"""Bounded SX UART capture; never toggles reset or programs firmware."""
import argparse
import errno
import fcntl
import math
import os
import select
import termios
import time
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--port', default='/dev/serial/by-id/usb-Silicon_Labs_CP2102N_USB_to_UART_Bridge_Controller_08db3e3b2974ea11adf500fb6a30b0f3-if00-port0')
p.add_argument('--seconds', type=float, default=3)
p.add_argument('--log', required=True)
p.add_argument('--reconnect', action='store_true', help='passive capture across USB reconnects; seconds is one total deadline')
p.add_argument('commands', nargs='*')
a = p.parse_args()
if not math.isfinite(a.seconds) or a.seconds <= 0:
    p.error('--seconds must be finite and positive')
if a.reconnect and a.commands:
    p.error('--reconnect is passive only; commands are never replayed after reconnect')
end = time.monotonic() + a.seconds
Path(a.log).parent.mkdir(parents=True, exist_ok=True)
with open(a.log, 'ab', buffering=0) as log:
    waiting = False
    while True:
        fd = None
        try:
            fd = os.open(a.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            settings = termios.tcgetattr(fd)
            settings[0] = settings[1] = settings[3] = 0
            settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            settings[4] = settings[5] = termios.B115200
            settings[6][termios.VMIN] = settings[6][termios.VTIME] = 0
            termios.tcsetattr(fd, termios.TCSANOW, settings)
            waiting = False
            log.write(f'\n# {time.strftime("%Y-%m-%dT%H:%M:%S%z")} port={a.port} baud=115200\n'.encode())
            for command in [None] + a.commands:
                if command is not None:
                    log.write(f'\n# TX {command!r}\n'.encode())
                    os.write(fd, command.encode('ascii') + b'\r\n')
                deadline = end if a.reconnect else time.monotonic() + a.seconds
                while time.monotonic() < deadline:
                    if select.select([fd], [], [], min(0.2, max(0, deadline-time.monotonic())))[0]:
                        data = os.read(fd, 65536)
                        if not data:
                            raise OSError(errno.ENODEV, 'SX UART disconnected')
                        log.write(data)
                        print(data.decode('utf-8', 'replace'), end='', flush=True)
            break
        except (OSError, termios.error) as error:
            if not a.reconnect or error.args[0] not in (errno.ENOENT, errno.ENODEV, errno.ENXIO, errno.EIO):
                raise
            if not waiting:
                log.write(f'\n# waiting for same USB by-id: {error}\n'.encode())
                waiting = True
        finally:
            if fd is not None:
                os.close(fd)
        remaining = end - time.monotonic()
        if remaining <= 0:
            break
        time.sleep(min(0.2, remaining))
