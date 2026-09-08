#!/usr/bin/env python3
"""Inventory actual build output, refusing to label a source-only tree as firmware.
Does not run a compiler, flash any device, or certify RF compatibility.
"""
from __future__ import annotations
import argparse, hashlib, json, pathlib, struct, sys

def item(path: pathlib.Path, root: pathlib.Path) -> dict:
    if not path.is_file() or not path.stat().st_size:
        raise ValueError(f'Missing/empty build output: {path}')
    return {'path':str(path.relative_to(root)), 'bytes':path.stat().st_size,
            'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

def collect_esp(root: pathlib.Path) -> dict:
    root=root.resolve()
    args=json.loads((root/'flasher_args.json').read_text())
    files=args.get('flash_files',{})
    if not files: raise ValueError('No flash_files in flasher_args.json')
    records=[]
    for offset,name in files.items():
        path=(root/name).resolve()
        if not path.is_relative_to(root): raise ValueError('Flash artifact escapes build directory')
        entry=item(path,root);entry['flash_offset']=offset;records.append(entry)
    elf=root/'halow_stage1.elf'
    raw=elf.read_bytes()
    if len(raw)<20 or raw[:4]!=b'\x7fELF' or raw[5]!=1 or struct.unpack_from('<H',raw,18)[0]!=94:
        raise ValueError('Application must be a little-endian Xtensa ELF, not a host test binary')
    return {'component':'esp32s3', 'status':'BUILD_OUTPUT_PRESENT_NOT_HARDWARE_TESTED',
            'files':records+[item(elf,root)], 'flasher_args':item(root/'flasher_args.json',root)}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--esp-build',type=pathlib.Path,required=True)
    ap.add_argument('--output',type=pathlib.Path,required=True)
    a=ap.parse_args()
    try:
        data=collect_esp(a.esp_build)
        a.output.write_text(json.dumps(data,indent=2)+'\n');print(json.dumps(data,indent=2))
    except (ValueError,OSError,KeyError) as e:
        print(f'ERROR: {e}',file=sys.stderr);return 1
    return 0
if __name__=='__main__':sys.exit(main())
