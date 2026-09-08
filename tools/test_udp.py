#!/usr/bin/env python3
"""Receive real ESP NRTU datagrams; compare sender counters with UDP delivery."""
import argparse
import ipaddress
import json
import os
import pathlib
import select
import socket
import struct
import sys
import threading
import time
import zlib

from hw_console import Console, passed


def decode(data):
    if len(data) != 1024:
        raise ValueError('NRTU datagram length is not 1024')
    magic, version, reserved, header, run, sequence, bps, elapsed, size, crc = struct.unpack_from('!4sBBH6I', data)
    if (magic, version, reserved, header, size) != (b'NRTU', 1, 0, 32, 992):
        raise ValueError('Invalid NRTU header')
    payload = data[32:]
    if zlib.crc32(payload) != crc or payload != bytes((sequence * 37 + index * 73) & 255 for index in range(size)):
        raise ValueError('NRTU payload corruption')
    return run, sequence, bps, elapsed


def summarize(sender, received, corrupt, duplicates, reordered, bps, seconds):
    if sender.get('event') != 'udp' or type(sender.get('error')) is not int:
        raise ValueError('Invalid UDP sender completion event')
    fields = ('run_id', 'sent', 'bytes', 'offered', 'duty_pauses', 'duty_blocked_ms', 'send_errors', 'elapsed_ms')
    if any(type(sender.get(key)) is not int or sender[key] < 0 for key in fields):
        raise ValueError('UDP sender completion is missing required nonnegative counters')
    if sender.get('bps') != bps or sender.get('seconds') != seconds:
        raise ValueError('UDP sender completion belongs to another rate/duration')
    if sender['bytes'] != sender['sent'] * 1024 or sender['offered'] < sender['sent'] or not sender['elapsed_ms']:
        raise ValueError('UDP sender byte/count/elapsed counters are inconsistent')
    if sender.get('duty_measurement') != 'sampled_target_availability':
        raise ValueError('UDP sender did not identify its independent duty measurement')
    seen = received.get(sender['run_id'], set())
    unexpected = len([sequence for sequence in seen if sequence >= sender['sent']])
    count = len(seen) - unexpected
    lost = sender['sent'] - count
    goodput = count * 1024 * 8000 / sender['elapsed_ms']
    ok = sender['sent'] > 0 and not (lost or corrupt or unexpected or sender['send_errors'] or sender['error'])
    return {
        'result': 'PASS' if ok else 'FAIL', 'criterion': 'all successfully sent datagrams received byte-exactly; no send errors',
        'requested_bps': bps, 'seconds': seconds, 'sent': sender['sent'], 'received': count,
        'received_bytes': count * 1024, 'lost': lost,
        'loss_fraction': lost / sender['sent'] if sender['sent'] else None,
        'corrupt': corrupt, 'unexpected_sequences': unexpected, 'duplicates': duplicates, 'out_of_order': reordered,
        'delivered_bps': round(goodput, 2), 'requested_rate_met': goodput >= bps * .9,
        'duty_blocked_ms': sender['duty_blocked_ms'], 'duty_pauses': sender['duty_pauses'],
        'duty_measurement': sender['duty_measurement'],
        'sender': sender,
    }


def receive_run(console, udp, source, bps, seconds, drain_seconds):
    completion, errors = [], []

    def transmit():
        try:
            completion.append(console.command(f'udp {bps} {seconds}', 'udp', seconds + 45))
        except Exception as exc:
            errors.append(str(exc))

    # Remove late packets from the previous rate before issuing the new command.
    while select.select([udp], [], [], 0)[0]:
        udp.recvfrom(65535)
    thread = threading.Thread(target=transmit, daemon=True)
    thread.start()
    received, highest = {}, {}
    corrupt = duplicates = reordered = 0
    end = time.monotonic() + seconds + 50
    finished_at = None
    while time.monotonic() < end:
        if not thread.is_alive() and finished_at is None:
            finished_at = time.monotonic()
        if finished_at is not None and time.monotonic() - finished_at >= drain_seconds:
            break
        if not select.select([udp], [], [], .1)[0]:
            continue
        data, remote = udp.recvfrom(65535)
        if remote[0] != source:
            continue
        # Existing video telemetry is independent of this test's exact sender duty counters.
        if len(data) == 40 and data[:4] == b'NRCS':
            continue
        try:
            run, sequence, packet_bps, elapsed = decode(data)
            if packet_bps != bps:
                continue
            if elapsed > seconds * 1000 + 1000:
                raise ValueError('NRTU elapsed exceeds requested duration')
            seen = received.setdefault(run, set())
            if sequence in seen:
                duplicates += 1
            else:
                if sequence < highest.get(run, -1):
                    reordered += 1
                highest[run] = max(sequence, highest.get(run, -1))
                seen.add(sequence)
        except ValueError:
            corrupt += 1
    thread.join(timeout=.5)
    if thread.is_alive():
        raise TimeoutError('ESP UDP command did not finish')
    if errors:
        raise ValueError(errors[0])
    if not completion:
        raise ValueError('ESP supplied no UDP completion event')
    result = summarize(completion[0], received, corrupt, duplicates, reordered, bps, seconds)
    result['receive_drain_seconds'] = drain_seconds
    result['loss_scope'] = 'end-to-end missing at receive deadline, including delayed/queued packets; not RF PER'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--bind', required=True)
    parser.add_argument('--udp-port', type=int, default=50000)
    parser.add_argument('--seconds', type=int, default=10)
    parser.add_argument('--drain-seconds', type=float, default=3)
    parser.add_argument('--rates', type=int, nargs='+', default=[100000, 250000, 500000, 1000000])
    parser.add_argument('--log-dir', type=pathlib.Path, required=True)
    args = parser.parse_args()
    console = None
    results = []
    try:
        if os.environ.get('STAGE1_WIRING_CONFIRMED') != '1' or os.environ.get('STAGE1_RF_CONFIRMED') != '1':
            raise ValueError('UDP NOT_TESTED: one-time wiring and JP RF confirmation required')
        for value in (args.source, args.bind):
            addr = ipaddress.IPv4Address(value)
            if addr.is_loopback or addr.is_unspecified or addr.is_multicast:
                raise ValueError('Use actual bench unicast IPv4 addresses, not local replay')
        if args.source == args.bind or not 1024 <= args.udp_port <= 65535:
            raise ValueError('Invalid peer addresses or UDP port')
        if not 1 <= args.seconds <= 60 or not 0 <= args.drain_seconds <= 30:
            raise ValueError('Use seconds 1..60 and drain-seconds 0..30')
        if not args.rates or any(rate not in (100000, 250000, 500000, 1000000) for rate in args.rates):
            raise ValueError('Supported rates: 100000 250000 500000 1000000')
        args.log_dir.mkdir(parents=True, exist_ok=True)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.bind((args.bind, args.udp_port))
            console = Console(args.port)
            console.command('pause')
            console.command('wiring-confirm')
            console.command('rf-confirm')
            passed(console.command('connect', 'connect', 60), 'connect')
            for bps in args.rates:
                result = receive_run(console, udp, args.source, bps, args.seconds, args.drain_seconds)
                results.append(result)
                (args.log_dir / f'udp_{bps}.json').write_text(json.dumps(result, indent=2) + '\n')
                print(json.dumps(result), flush=True)
                # Stop escalation after the first failed integrity/delivery check.
                if result['result'] != 'PASS':
                    break
    except (OSError, ValueError, TimeoutError) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        return 1
    finally:
        if console:
            console.close()
        if args.log_dir.is_dir():
            (args.log_dir / 'udp_results.json').write_text(json.dumps(results, indent=2) + '\n')
    return 0 if len(results) == len(args.rates) and all(item['result'] == 'PASS' for item in results) else 1


if __name__ == '__main__':
    sys.exit(main())
