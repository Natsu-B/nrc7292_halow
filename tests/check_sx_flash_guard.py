#!/usr/bin/env python3
"""Verify the real ARM link refuses SDK boot-time flash-map migration."""
import re
import subprocess
import sys

asm = subprocess.check_output(['arm-none-eabi-objdump', '-d', sys.argv[1]], text=True)

def body(name):
    match = re.search(r'^([0-9a-f]+) <' + name + r'>:\n(.*?)(?=\n[0-9a-f]+ <|\Z)', asm, re.M | re.S)
    assert match, 'missing ARM symbol: ' + name
    return int(match[1], 16), match[2]

address, guard = body('nrc_sf_migration_1m_to_2m')
opcodes = re.findall(r'^\s*[0-9a-f]+:\s+([0-9a-f]{4}(?: [0-9a-f]{4})?)\s+', guard, re.M)
assert opcodes == ['2004', '4770'], 'override must be only movs r0,#4; bx lr: ' + guard
_, init = body('nrc_sf_init_memory_map')
call = re.search(r'\bbl\s+([0-9a-f]+) <((?:__)?nrc_sf_migration_1m_to_2m(?:_veneer)?)>', init)
assert call, 'SDK bypasses migration override'
if call[2].endswith('_veneer'):
    veneer_address, veneer = body(call[2])
    assert int(call[1], 16) == veneer_address and re.search(r'ldr\.w\s+pc, \[pc\]', veneer)
    target = re.search(r'\.word\s+0x([0-9a-f]+)', veneer)
    assert target and int(target[1], 16) == (address | 1), 'veneer bypasses override'
else:
    assert int(call[1], 16) == address, 'SDK bypasses migration override'
tail = init[call.end():]
assert re.search(r'cmp\s+r0, #1.*?bls\.n.*?cmp\s+r0, #5.*?bne\.n', tail, re.S), 'unexpected SDK result handling'
print('PASS: ARM migration override returns SF_MIGRATION_RES_ERROR (4); SDK calls override')
