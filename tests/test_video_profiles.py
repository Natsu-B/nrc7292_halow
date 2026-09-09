#!/usr/bin/env python3
"""Malformed bundle checks and the actual ESP clip reader with a RAM-backed partition."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from prepare_video_profiles import PROFILE_SIZES, bundle, validate_clip, validate_video
from make_clip import make


def fixture(width, height, fps, independent):
    key = b'\0\0\0\1\x67\x42\0\0\0\1\x68\xee\0\0\0\1\x65\x88'
    p = b'\0\0\0\1\x41\x88'
    frames = [b'\0\0\0\1\x09\xf0' + (key if independent or i % 10 == 0 else p)
              for i in range(fps * 12)]
    return make(b''.join(frames), 1, width, height, fps)[0]


def repair_outer(data):
    struct.pack_into('<I', data, 16, zlib.crc32(data[32:112]))
    struct.pack_into('<I', data, 28, zlib.crc32(data[:28]))


class VideoProfiles(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.clips = [fixture(w, h, 10 if i == 0 else 5, i != 0)
                     for i, (w, h) in enumerate(PROFILE_SIZES)]
        cls.data = bundle(cls.clips)

    def test_legacy_and_bundle(self):
        self.assertEqual(validate_video(self.clips[0])['format'], 'HVC1')
        meta = validate_video(self.data)
        self.assertEqual(len(meta['profiles']), 5)
        self.assertEqual(meta['profiles'][4]['height'], 120)
        start, size = struct.unpack_from('<II', self.data, 32)
        self.assertEqual(self.data[start:start + size], self.clips[0])
        validate_clip((ROOT / 'assets/video.bin').read_bytes())

    def test_header_and_index_crc(self):
        for at in (12, 16, 28, 32, 48):
            data = bytearray(self.data); data[at] ^= 1
            with self.assertRaises(ValueError): validate_video(data)

    def test_overlap_bounds_reserved_and_flags(self):
        for at, value in ((48, 112), (48, 0xfffffff0), (52, 0xffffffff),
                          (60, 1), (56, 0), (20, 1), (24, 1)):
            data = bytearray(self.data)
            struct.pack_into('<I', data, at, value); repair_outer(data)
            with self.assertRaises(ValueError): validate_video(data)

    def test_inner_crc_and_dependent_snapshot(self):
        data = bytearray(self.data)
        start = struct.unpack_from('<I', data, 48)[0]
        data[start + 48] ^= 1
        with self.assertRaises(ValueError): validate_video(data)
        bad = list(self.clips)
        bad[1] = fixture(640, 360, 5, False)
        with self.assertRaises(ValueError): bundle(bad)

    def test_trailing_and_oversize(self):
        with self.assertRaises(ValueError): validate_video(self.data + b'\0')
        with self.assertRaises(ValueError): validate_video(b'\0' * (2 * 1024 * 1024 + 1))

    def test_actual_c_reader(self):
        compiler = shutil.which('cc') or shutil.which('gcc')
        self.assertIsNotNone(compiler, 'A C compiler is required for the real reader check')
        with tempfile.TemporaryDirectory(prefix='hvb-reader-') as name:
            tmp = Path(name)
            (tmp / 'bundle.bin').write_bytes(self.data)
            (tmp / 'legacy.bin').write_bytes(self.clips[0])
            (tmp / 'esp_partition.h').write_text('''#pragma once
#include <stddef.h>
#include <stdint.h>
#define ESP_OK 0
#define ESP_PARTITION_TYPE_DATA 1
typedef struct { uint32_t size; } esp_partition_t;
const esp_partition_t *esp_partition_find_first(int,int,const char*);
int esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
''')
            (tmp / 'esp_log.h').write_text('#define ESP_LOGE(...) ((void)0)\n')
            (tmp / 'reader.c').write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_partition.h"
#include "clip_source.h"
static esp_partition_t partition;
static unsigned char *bytes;
const esp_partition_t *esp_partition_find_first(int t,int s,const char *n){return &partition;}
int esp_partition_read(const esp_partition_t *p,size_t at,void *dst,size_t n){
    if(at>p->size || n>p->size-at)return -1;memcpy(dst,bytes+at,n);return 0;
}
static void load(const char *path){
    free(bytes);FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);
    partition.size=(uint32_t)ftell(f);rewind(f);bytes=malloc(partition.size);assert(bytes);
    assert(fread(bytes,1,partition.size,f)==partition.size);fclose(f);
}
static uint32_t le32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
int main(int argc,char **argv){
    assert(argc==3);load(argv[1]);hvc_clip_t original,other;hvc_frame_t frame;unsigned char buf[2048];
    assert(clip_source_open(&original)==0);assert(clip_source_profile_count()==5);
    assert(clip_source_max_frame()>0 && clip_source_max_frame()<=sizeof buf);
    for(unsigned i=0;i<5;i++)assert(clip_source_select(&other,i)==0);
    assert(clip_source_latest(&original,1201,buf,sizeof buf,&frame)==0);
    assert(frame.pts_ms==1200 && original.frame_index==13 && !(frame.flags&1));
    assert(hvc_clip_next(&original,buf,sizeof buf,&frame)==0 && frame.pts_ms==1300);
    assert(clip_source_select(&other,4)==0);
    assert(clip_source_latest(&other,199,buf,sizeof buf,&frame)==0 && frame.pts_ms==0 && other.frame_index==1);
    uint32_t before=other.position;
    assert(clip_source_latest(&other,201,buf,1,&frame)==-2 && other.position==before);
    assert(clip_source_latest(&other,201,buf,sizeof buf,&frame)==0 && frame.pts_ms==200 && other.frame_index==2);
    assert(clip_source_latest(&other,99999,buf,sizeof buf,&frame)==0 && frame.pts_ms==11800);
    assert(hvc_clip_next(&other,buf,sizeof buf,&frame)==1);
    uint32_t at=le32(bytes+48);bytes[at+48]^=1;
    assert(clip_source_open(&other)==-1 && clip_source_profile_count()==0);bytes[at+48]^=1;
    put32(bytes+48,112);put32(bytes+16,hvc_crc32(bytes+32,80));put32(bytes+28,hvc_crc32(bytes,28));
    assert(clip_source_open(&other)==-1);
    load(argv[2]);assert(clip_source_open(&original)==0 && clip_source_profile_count()==1);
    assert(clip_source_select(&other,1)==-1);
    assert(clip_source_latest(&original,199,buf,sizeof buf,&frame)==0 && frame.pts_ms==100);
    assert(clip_source_latest_key(&original,600,buf,sizeof buf,&frame)==0);
    assert(frame.pts_ms==0 && (frame.flags&1) && original.frame_index==1);
    assert(hvc_clip_next(&original,buf,sizeof buf,&frame)==0 && frame.pts_ms==100);
    assert(clip_source_latest_key(&original,1600,buf,sizeof buf,&frame)==0);
    assert(frame.pts_ms==1000 && (frame.flags&1) && original.frame_index==11);
    assert(hvc_clip_next(&original,buf,sizeof buf,&frame)==0 && frame.pts_ms==1100);
    free(bytes);puts("C clip source bounds/CRC/latest/cursor/context PASS");return 0;
}
''')
            command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-misleading-indentation',
                       '-I' + str(tmp), '-I' + str(ROOT / 'esp32/main'),
                       '-I' + str(ROOT / 'components/hvc_clip/include'),
                       '-I' + str(ROOT / 'components/nrc_video_packet/include'),
                       str(tmp / 'reader.c'), str(ROOT / 'esp32/main/clip_source.c'),
                       str(ROOT / 'components/hvc_clip/src/hvc_clip.c'),
                       str(ROOT / 'components/nrc_video_packet/src/nrc_video_packet.c'),
                       '-o', str(tmp / 'reader')]
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(tmp / 'reader'), str(tmp / 'bundle.bin'), str(tmp / 'legacy.bin')],
                                    check=True, capture_output=True, text=True)
            self.assertIn('PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
