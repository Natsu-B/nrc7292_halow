#!/usr/bin/env python3
"""Write an HVC1 clip or HVB1 bundle to ESP32-S3 after validating the actual partition table.
No erase-all and no SX flashing. Opens the explicit ESP port; ROM connection can reset the ESP.
Run in the ESP-IDF Python environment. The application must be stopped during Flash writes.
"""
from __future__ import annotations
import argparse, hashlib, importlib.util, pathlib, struct, subprocess, sys, tempfile
if __package__:
    from .prepare_video_profiles import validate_clip, validate_video
else:
    _spec = importlib.util.spec_from_file_location('_hvb_profiles', pathlib.Path(__file__).with_name('prepare_video_profiles.py'))
    _profiles = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_profiles)
    validate_clip, validate_video = _profiles.validate_clip, _profiles.validate_video
ROOT=pathlib.Path(__file__).resolve().parents[1]

def video_partition(b: bytes) -> tuple[int,int]:
    entries=[];md5_checked=False
    for at in range(0,min(len(b),0xc00),32):
        raw=b[at:at+32]
        if len(raw)!=32:raise ValueError('Truncated partition table')
        magic=struct.unpack_from('<H',raw)[0]
        if magic==0xffff:break
        if magic==0xebeb:
            if hashlib.md5(b[:at]).digest()!=raw[16:32]:raise ValueError('Partition table MD5 mismatch')
            md5_checked=True;break
        if magic!=0x50aa:raise ValueError('Invalid partition entry magic')
        _,kind,subtype,offset,size,label,flags=struct.unpack('<HBBII16sI',raw)
        if not size or offset<0x9000 or offset+size>0x400000:raise ValueError('Not the supported 4 MiB Stage1 layout')
        for e in entries:
            if offset<e['offset']+e['size'] and e['offset']<offset+size:raise ValueError('Overlapping partitions')
        entries.append(dict(kind=kind,subtype=subtype,offset=offset,size=size,label=label.split(b'\0',1)[0],flags=flags))
    if not md5_checked:raise ValueError('Partition table MD5 required for this write tool')
    matches=[e for e in entries if e['label']==b'video' and e['kind']==1]
    if len(matches)!=1:raise ValueError('Need exactly one data/video partition')
    e=matches[0]
    if e['offset']!=0x200000 or e['size']!=0x200000 or e['flags']:
        raise ValueError('Installed video partition does not match unencrypted Stage1 layout')
    return e['offset'],e['size']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',required=True)
    p.add_argument('--clip',type=pathlib.Path,default=ROOT/'assets/video.bin')
    p.add_argument('--confirm-stage1-partitions',action='store_true')
    a=p.parse_args()
    if not a.confirm_stage1_partitions:p.error('Explicit --confirm-stage1-partitions required; actual table is also verified')
    try:
        b=a.clip.read_bytes();validate_video(b)
        port=pathlib.Path(a.port)
        if port.parent != pathlib.Path('/dev/serial/by-id') or not port.exists():
            raise ValueError('An existing ESP /dev/serial/by-id path is required')
        base=[sys.executable,'-m','esptool','--chip','esp32s3','--port',a.port]
        with tempfile.TemporaryDirectory(prefix='halow-flash-') as tmp:
            table=pathlib.Path(tmp)/'partitions.bin'
            # Explicit --chip and no --force: esptool rejects a different target.
            subprocess.run(base+['read_flash','0x8000','0x1000',str(table)],check=True)
            offset,size=video_partition(table.read_bytes())
            if len(b)>size:raise ValueError('Clip exceeds video partition')
            print(f'Writing ESP video only: offset={offset:#x}, size={len(b)}, SHA256={hashlib.sha256(b).hexdigest()}',flush=True)
            subprocess.run(base+['write_flash',hex(offset),str(a.clip.resolve())],check=True)
            check=pathlib.Path(tmp)/'readback.bin'
            subprocess.run(base+['read_flash',hex(offset),str(len(b)),str(check)],check=True)
            if check.read_bytes()!=b:raise ValueError('Flash read-back differs')
        print('PASS: ESP video read-back byte-for-byte match. SX was not flashed.')
    except (OSError,ValueError,subprocess.CalledProcessError) as e:
        print(f'ERROR: {e}',file=sys.stderr);return 1
    return 0
if __name__=='__main__':sys.exit(main())
