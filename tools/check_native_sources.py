#!/usr/bin/env python3
"""Check vendored native transport provenance and byte-for-byte shared L2 ABI."""
import hashlib,json,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
base=root/'third_party/newracom'
for name,digest in json.loads((base/'SHA256.json').read_text()).items():
    if hashlib.sha256((base/name).read_bytes()).hexdigest()!=digest:sys.exit('Native source changed: '+name)
for name,host in [('nrc_l2_protocol.h','include'),('nrc_l2_protocol.c','src')]:
    if (root/'components/nrc7292'/host/name).read_bytes()!=(root/'sx_newah/L2BRIDGE_HSPI'/name).read_bytes():sys.exit('Shared ABI differs: '+name)
print('PASS: seven native transport originals and host/target L2 source identity')
