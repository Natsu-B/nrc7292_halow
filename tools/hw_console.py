#!/usr/bin/env python3
"""Linux USB serial diagnostics without pyserial; never resets SX or toggles DTR."""
import argparse
import fcntl
import json
import os
import re
import select
import sys
import termios
import time
import tty


def check_esp_failure(line):
    line = re.sub(r'\x1b\[[0-9;]*m', '', line).strip()
    if (line.startswith(('***ERROR*** A stack overflow in task ', 'Guru Meditation Error:',
                         'abort() was called at PC ', 'assert failed:', 'Stack smashing protect failure!'))
            or line == 'Rebooting...'):
        raise ValueError(f'ESP instability detected: {line}')


def event(line):
    try:
        value = json.loads(line[line.index('{'):])
        return value if isinstance(value, dict) else None
    except (ValueError, json.JSONDecodeError):
        return None


def passed(value, name, count=None, size=None):
    if value.get('event') != name or value.get('error') != 0:
        raise ValueError(f'{name} did not succeed: {value}')
    if name == 'loopback' and (value.get('requested') != count or value.get('passed') != count or value.get('bytes') != size):
        raise ValueError(f'Incomplete loopback: {value}')
    if name == 'ping' and (type(value.get('sent')) is not int or value['sent'] <= 0 or value.get('received') != value['sent']):
        raise ValueError(f'Incomplete ping: {value}')


def validate_probe(diag, candidate):
    expected_cs = {'ce0': 6, 'ce1': 5}.get(candidate)
    if (diag.get('event') != 'diag' or diag.get('opened') is False or diag.get('faulted') != 0
            or diag.get('chip_id') != 0x7292 or diag.get('signature') != 'NRC-HSPI'
            or diag.get('cs_gpio') not in (5, 6) or diag.get('clock_hz') not in (1000000, 5000000)
            or (expected_cs is not None and diag.get('cs_gpio') != expected_cs)):
        raise ValueError(f'Probe diagnostics do not prove the requested native HSPI candidate: {diag}')
    for direction in ('rx', 'tx'):
        count, size = diag.get(direction + '_slot_num'), diag.get(direction + '_slot_size')
        if type(count) is not int or count <= 0 or type(size) is not int or not 4 < size <= 512 or size % 4:
            raise ValueError(f'Invalid {direction} native queue geometry')


class Console:
    def __init__(self, path):
        if not path.startswith('/dev/serial/by-id/'):
            raise ValueError('Use /dev/serial/by-id/ identity, never a numbered tty.')
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        try:
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.saved = termios.tcgetattr(self.fd)
            tty.setraw(self.fd)
            attrs = termios.tcgetattr(self.fd)
            attrs[2] |= termios.CLOCAL | termios.CREAD
            attrs[2] &= ~termios.HUPCL
            attrs[4] = attrs[5] = termios.B115200
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
        except BaseException:
            os.close(self.fd)
            raise
        self.pending = b''

    def close(self):
        try:
            termios.tcsetattr(self.fd, termios.TCSANOW, self.saved)
            fcntl.ioctl(self.fd, termios.TIOCNXCL)
        finally:
            os.close(self.fd)

    def read(self, duration, expected=None):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.fd], [], [], max(0, deadline - time.monotonic()))
            if not ready:
                break
            chunk = os.read(self.fd, 8192)
            if not chunk:
                raise OSError('USB serial disconnected')
            sys.stdout.write(chunk.decode('utf-8', errors='replace'))
            sys.stdout.flush()
            self.pending += chunk
            matched = None
            while b'\n' in self.pending:
                line, self.pending = self.pending.split(b'\n', 1)
                line = line.decode('utf-8', errors='replace').strip()
                check_esp_failure(line)
                obj = event(line)
                if expected and obj and obj.get('event') == expected and matched is None:
                    matched = obj
            check_esp_failure(self.pending.decode('utf-8', errors='replace'))
            if len(self.pending) > 65536:
                self.pending = self.pending[-65536:]
            if matched is not None:
                return matched
        if expected:
            raise TimeoutError(f'No {expected} completion event within {duration:g}s')

    def command(self, command, expected=None, timeout=30):
        if '\n' in command or '\r' in command or not command or len(command.encode()) > 126:
            raise ValueError('Command must be one console line, at most 126 bytes')
        # Consume previous command output before matching its successor's event.
        self.read(.15)
        print(f'\nHOST command: {command}', flush=True)
        os.write(self.fd, (command + '\n').encode())
        return self.read(timeout if expected else .5, expected)


def hspi(console, args):
    console.command('pins')
    if os.environ.get('STAGE1_WIRING_CONFIRMED') != '1':
        raise ValueError('HSPI NOT_TESTED: set STAGE1_WIRING_CONFIRMED=1 after the one-time GND/3.3V/pin check.')
    console.command('wiring-confirm')
    command = 'probe' if args.candidate == 'auto' else 'probe-' + args.candidate
    passed(console.command(command, 'probe', 45), 'probe')
    before = console.command('diag', 'diag')
    validate_probe(before, args.candidate)
    cs, clock = before['cs_gpio'], before['clock_hz']
    print(f'HSPI {clock // 1000000}MHz PASS: GPIO{cs}/CE{0 if cs == 6 else 1}; native chip/signature/geometry verified')
    if clock == 1000000:
        print('This result supplies no successful 5MHz probe evidence; inspect the candidate logs.')
    if args.probe_only:
        print('HSPI probe PASS; loopback and EIRQ NOT_TESTED')
        return
    if args.eirq:
        console.command('eirq')
    for count, size in ((100, 64), (100, 512), (1000, 1518)):
        result = console.command(f'loopback {count} {size}', 'loopback', 180)
        passed(result, 'loopback', count, size)
    after = console.command('diag', 'diag')
    validate_probe(after, args.candidate)
    if (after['cs_gpio'], after['clock_hz']) != (cs, clock):
        raise ValueError('HSPI CS or clock changed during loopback')
    errors = ('spi_errors', 'bad_ack', 'bad_header', 'seq_gaps', 'invalid_status')
    for key in errors:
        if key not in before or key not in after or after[key] != before[key]:
            raise ValueError(f'HSPI counter changed or missing: {key} {before.get(key)} -> {after.get(key)}')
    if after.get('faulted'):
        raise ValueError('HSPI transport faulted')
    print('HSPI loopback PASS: 100x64, 100x512, 1000x1518; corruption/sequence/SPI errors=0')
    if args.eirq:
        if (not after.get('eirq_enabled') or after.get('irq_count', 0) <= before.get('irq_count', 0)
                or after.get('irq_high_count', 0) <= before.get('irq_high_count', 0)):
            raise ValueError('EIRQ FAIL: no enabled level-high interrupt activity')
        print('EIRQ GPIO4 PASS: level-high mode with interrupt activity')
    else:
        print('EIRQ GPIO4 NOT_TESTED: polling session; run --eirq after polling succeeds')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    sub = parser.add_subparsers(dest='mode', required=True)
    monitor = sub.add_parser('monitor')
    monitor.add_argument('--seconds', type=float, default=15)
    command = sub.add_parser('command')
    command.add_argument('text')
    command.add_argument('--event')
    command.add_argument('--timeout', type=float, default=45)
    command.add_argument('--after-seconds', type=float, default=0)
    probe = sub.add_parser('hspi')
    probe.add_argument('--candidate', choices=('auto', 'ce0', 'ce1'), default='auto')
    probe.add_argument('--probe-only', action='store_true')
    probe.add_argument('--eirq', action='store_true')
    args = parser.parse_args()
    console = None
    try:
        console = Console(args.port)
        if args.mode == 'monitor':
            if args.seconds <= 0:
                raise ValueError('--seconds must be positive')
            console.read(args.seconds)
        elif args.mode == 'command':
            value = console.command(args.text, args.event, args.timeout)
            if args.event and 'error' in value:
                passed(value, args.event)
            if args.after_seconds > 0:
                console.read(args.after_seconds)
        else:
            hspi(console, args)
    except (OSError, ValueError, TimeoutError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    finally:
        if console:
            console.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
