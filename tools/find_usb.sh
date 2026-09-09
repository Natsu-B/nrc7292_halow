#!/usr/bin/env bash
set -euo pipefail
python3 - "${1:-all}" <<'PY'
import glob, os, pathlib, subprocess, sys
kind = sys.argv[1]
if kind not in ('esp', 'sx', 'all'):
    sys.exit('Usage: find_usb.sh [esp|sx|all]')
found = {'esp': [], 'sx': []}
for name in sorted(glob.glob('/dev/serial/by-id/*')):
    p = subprocess.run(['udevadm', 'info', '--query=property', '--name', name],
                       text=True, capture_output=True, check=True)
    info = dict(line.split('=', 1) for line in p.stdout.splitlines() if '=' in line)
    vid, pid, serial = (info.get(k, '') for k in ('ID_VENDOR_ID', 'ID_MODEL_ID', 'ID_SERIAL_SHORT'))
    if (vid, pid) == ('303a', '1001') and serial == os.environ.get('ESP_USB_SERIAL', '90:70:69:10:C7:B4'):
        found['esp'].append(name)
    if (vid, pid) == ('10c4', 'ea60') and serial == os.environ.get('SX_USB_SERIAL', '08db3e3b2974ea11adf500fb6a30b0f3'):
        found['sx'].append(name)
    if kind == 'all':
        print(f'{name}\n  node={pathlib.Path(name).resolve()} VID:PID={vid}:{pid} serial={serial}')
if kind != 'all':
    requested = os.environ.get(kind.upper() + '_BY_ID')
    candidates = [p for p in found[kind] if not requested or p == requested]
    if len(candidates) != 1:
        sys.exit(f'Expected one {kind} USB with confirmed serial, found {len(candidates)}; check {kind.upper()}_USB_SERIAL and {kind.upper()}_BY_ID.')
    print(candidates[0])
PY
