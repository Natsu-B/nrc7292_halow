#!/usr/bin/env python3
"""Record bounded wired AP checks without changing AP settings."""
import argparse
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--interface', default='enp0s31f6')
p.add_argument('--log', default=str(Path(__file__).resolve().parents[1] / 'logs/ap_probe.log'))
p.add_argument('addresses', nargs='+')
a = p.parse_args()
Path(a.log).parent.mkdir(parents=True, exist_ok=True)
with open(a.log, 'a') as log:
    commands = [['date', '--iso-8601=seconds'], ['ip', '-br', 'addr', 'show', a.interface]]
    commands += [['ping', '-I', a.interface, '-c', '3', '-W', '1', ip] for ip in a.addresses]
    commands += [['ip', 'neigh', 'show', 'dev', a.interface]]
    for command in commands:
        result = subprocess.run(command, capture_output=True, text=True, timeout=10)
        log.write('\n$ ' + ' '.join(command) + '\n' + result.stdout + result.stderr)
        log.write(f'EXIT={result.returncode}\n')
        print(' '.join(command), f'EXIT={result.returncode}')
