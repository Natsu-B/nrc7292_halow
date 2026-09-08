#!/usr/bin/env python3
"""USB identity guard with synthetic udev properties; never opens real hardware."""
import contextlib
import io
import os
import pathlib
import sys
from types import SimpleNamespace
from unittest.mock import patch

path = pathlib.Path(__file__).resolve().parents[1] / 'tools/find_usb.sh'
body = path.read_text().split("<<'PY'\n", 1)[1].rsplit('\nPY', 1)[0]
known = '/dev/serial/by-id/usb-Espressif_known-if00'
replacement = '/dev/serial/by-id/usb-Espressif_replacement-if00'


def discover(devices, overrides=None):
    def udev(args, **kwargs):
        serial = devices[args[-1]]
        return SimpleNamespace(stdout=f'ID_VENDOR_ID=303a\nID_MODEL_ID=1001\nID_SERIAL_SHORT={serial}\n')
    output = io.StringIO()
    with patch('glob.glob', return_value=list(devices)), patch('subprocess.run', side_effect=udev), \
            patch.object(sys, 'argv', ['-', 'esp']), patch.dict(os.environ, overrides or {}, clear=True), \
            contextlib.redirect_stdout(output):
        exec(compile(body, str(path), 'exec'), {'__name__': '__main__'})
    return output.getvalue().strip()


assert discover({known: '90:70:69:10:C7:B4'}) == known
assert discover({known: '90:70:69:10:C7:B4', replacement: 'AA:BB:CC:DD:EE:FF'}) == known
for overrides in ({}, {'ESP_BY_ID': replacement}):
    try:
        discover({replacement: 'AA:BB:CC:DD:EE:FF'}, overrides)
        raise AssertionError('An unconfirmed replacement ESP was accepted')
    except SystemExit:
        pass
assert discover({replacement: 'AA:BB:CC:DD:EE:FF'}, {'ESP_USB_SERIAL': 'AA:BB:CC:DD:EE:FF'}) == replacement
print('PASS: ESP discovery pins the confirmed serial; another board requires explicit ESP_USB_SERIAL.')
