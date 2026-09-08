#!/usr/bin/env python3
"""Verify source-package files against manifest.sha256."""
import hashlib,pathlib,sys
root=pathlib.Path(__file__).resolve().parents[1]
count=0
for line in (root/'manifest.sha256').read_text().splitlines():
    digest,name=line.split('  ',1);p=(root/name).resolve()
    if root not in p.parents or not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:
        sys.exit('FAILED: '+name)
    count+=1
print(f'PASS: {count} package files match manifest.sha256')
