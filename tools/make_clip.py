#!/usr/bin/env python3
"""Package Annex-B H.264 with AUDs, or concatenated JPEGs, as a checked HVC1 clip.
FFmpeg is a developer-side input generator only, never a receiver dependency.
"""
import argparse, json, re, struct, zlib
from pathlib import Path

def nal_units(data):
    starts=list(re.finditer(b'\x00\x00(?:\x00)?\x01',data))
    for i,m in enumerate(starts):
        end=starts[i+1].start() if i+1<len(starts) else len(data)
        body=data[m.end():end]
        if body:yield body[0]&31,data[m.start():end]

def h264_frames(data):
    current=bytearray();have_picture=False
    for kind,raw in nal_units(data):
        if kind==9 and have_picture:
            yield bytes(current);current.clear();have_picture=False
        current.extend(raw)
        have_picture|=kind in (1,5)
    if have_picture:yield bytes(current)

def jpeg_frames(data):
    # FFmpeg-produced MJPEG: SOI/EOI markers do not appear unescaped in entropy data.
    at=0
    while at<len(data):
        s=data.find(b'\xff\xd8',at)
        if s<0:break
        e=data.find(b'\xff\xd9',s+2)
        if e<0:raise ValueError('truncated JPEG')
        yield data[s:e+2];at=e+2

def make(data,codec,width,height,fps):
    frames=list(h264_frames(data) if codec==1 else jpeg_frames(data))
    if not frames:raise ValueError('No frames. H.264 requires Annex-B and aud=1.')
    duration=(len(frames)*1000+fps-1)//fps
    header=struct.pack('<4sHHBBHHHHHII',b'HVC1',1,32,codec,0,width,height,0,fps,1,len(frames),duration)
    assert len(header)==28
    out=bytearray(header+struct.pack('<I',zlib.crc32(header)))
    for i,f in enumerate(frames):
        if not 0<len(f)<=256*1024:raise ValueError('frame exceeds 256 KiB')
        kinds={t for t,_ in nal_units(f)} if codec==1 else set()
        key=codec==2 or 5 in kinds
        if codec==1 and key and not {7,8,5}.issubset(kinds):raise ValueError('IDR must repeat SPS/PPS')
        if i==0 and not key:raise ValueError('first frame must be key')
        out+=struct.pack('<IIII',len(f),i*1000//fps,int(key),zlib.crc32(f))+f
    return bytes(out),dict(codec='H264' if codec==1 else 'MJPEG',width=width,height=height,fps=fps,frames=len(frames),duration_ms=duration,
                          max_frame_bytes=max(map(len,frames)),payload_bps=sum(map(len,frames))*8*1000//duration,bytes=len(out))
def main():
    ap=argparse.ArgumentParser();ap.add_argument('input',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--codec',choices=['h264','mjpeg'],default='h264');ap.add_argument('--width',type=int,required=True);ap.add_argument('--height',type=int,required=True);ap.add_argument('--fps',type=int,required=True)
    a=ap.parse_args()
    if not 1<=a.fps<=60 or not 1<=a.width<=1920 or not 1<=a.height<=1088:ap.error('unsupported dimensions/fps')
    out,meta=make(a.input.read_bytes(),1 if a.codec=='h264' else 2,a.width,a.height,a.fps)
    a.output.write_bytes(out);a.output.with_suffix('.json').write_text(json.dumps(meta,indent=2)+'\n');print(json.dumps(meta))
if __name__=='__main__':main()
