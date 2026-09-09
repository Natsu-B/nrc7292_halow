#!/usr/bin/env python3
"""Build/validate an HVB1 recorded-video bundle; never opens a hardware device."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

if __package__:
    from .make_clip import make, nal_units
else:
    _spec = importlib.util.spec_from_file_location('_hvb_make_clip', Path(__file__).with_name('make_clip.py'))
    _builder = importlib.util.module_from_spec(_spec)
    _spec.loader.exec_module(_builder)
    make, nal_units = _builder.make, _builder.nal_units

ROOT = Path(__file__).resolve().parents[1]
MAX_VIDEO_BYTES = 2 * 1024 * 1024
PROFILE_SIZES = ((640, 360), (640, 360), (426, 240), (320, 180), (214, 120))


def records(data: bytes):
    """Iterate records after validate_clip has established their bounds."""
    at = 32
    for _ in range(struct.unpack_from('<I', data, 20)[0]):
        size, pts, flags, crc = struct.unpack_from('<IIII', data, at)
        at += 16
        yield pts, flags, data[at:at + size]
        at += size


def validate_clip(data: bytes, independent: bool = False) -> dict:
    if (len(data) < 32 or data[:4] != b'HVC1' or
            struct.unpack_from('<HH', data, 4) != (1, 32) or
            zlib.crc32(data[:28]) != struct.unpack_from('<I', data, 28)[0]):
        raise ValueError('Invalid HVC1 header/CRC')
    codec = data[8]
    width, height, reserved, fps_num, fps_den = struct.unpack_from('<HHHHH', data, 10)
    count, duration = struct.unpack_from('<II', data, 20)
    if (data[9] or reserved or codec not in (1, 2) or not 0 < width <= 1920 or
            not 0 < height <= 1088 or not fps_num or not fps_den or
            not 0 < count <= 100000 or not duration):
        raise ValueError('Invalid HVC1 geometry/timing')
    at, previous, maximum, payload_size = 32, -1, 0, 0
    for i in range(count):
        if at + 16 > len(data):
            raise ValueError('Truncated HVC1 frame record')
        size, pts, flags, crc = struct.unpack_from('<IIII', data, at)
        at += 16
        if (not 0 < size <= 256 * 1024 or at + size > len(data) or
                pts <= previous or pts >= duration or flags & ~1 or
                (i == 0 and not flags & 1)):
            raise ValueError('Invalid HVC1 frame range/timing/flags')
        payload = data[at:at + size]
        if zlib.crc32(payload) != crc:
            raise ValueError('HVC1 frame CRC mismatch')
        if independent and (codec != 1 or not flags & 1):
            raise ValueError('Snapshot profile contains a dependent frame')
        if codec == 1:
            kinds = {kind for kind, _ in nal_units(payload)}
            if not kinds.intersection((1, 5)):
                raise ValueError('H264 frame has no picture')
            if flags & 1 and (not {5, 7, 8}.issubset(kinds) or 1 in kinds):
                raise ValueError('H264 keyframe must contain SPS/PPS/IDR without P slices')
        previous = pts
        maximum = max(maximum, size)
        payload_size += size
        at += size
    if at != len(data):
        raise ValueError('Unexpected trailing HVC1 bytes')
    return dict(codec=codec, width=width, height=height, fps_num=fps_num, fps_den=fps_den,
                frames=count, duration_ms=duration, max_frame_bytes=maximum,
                payload_bps=payload_size * 8000 // duration, bytes=len(data),
                independent=independent, sha256=hashlib.sha256(data).hexdigest())


def validate_video(data: bytes) -> dict:
    """Shared pre-flash validator for legacy HVC1 and five-profile HVB1."""
    if len(data) > MAX_VIDEO_BYTES:
        raise ValueError('Video exceeds the 2 MiB partition')
    if data[:4] == b'HVC1':
        return dict(format='HVC1', bytes=len(data), profiles=[validate_clip(data)])
    if (len(data) < 112 or data[:4] != b'HVB1' or
            struct.unpack_from('<HHII', data, 4) != (1, 32, 5, len(data)) or
            struct.unpack_from('<II', data, 20) != (0, 0) or
            zlib.crc32(data[:28]) != struct.unpack_from('<I', data, 28)[0] or
            zlib.crc32(data[32:112]) != struct.unpack_from('<I', data, 16)[0]):
        raise ValueError('Invalid HVB1 header/index/CRC')
    profiles, ranges = [], []
    for i in range(5):
        offset, length, flags, reserved = struct.unpack_from('<IIII', data, 32 + i * 16)
        if (offset < 112 or length < 32 or offset + length > len(data) or reserved or
                flags != int(i != 0) or
                any(offset < end and start < offset + length for start, end in ranges)):
            raise ValueError('Invalid or overlapping HVB1 profile range/flags')
        ranges.append((offset, offset + length))
        profile = validate_clip(data[offset:offset + length], independent=bool(flags))
        if ((profile['width'], profile['height']) != PROFILE_SIZES[i] or profile['codec'] != 1 or
                profile['fps_num'] != (10 if i == 0 else 5) or profile['fps_den'] != 1 or
                (profiles and profile['duration_ms'] != profiles[0]['duration_ms'])):
            raise ValueError('Unexpected HVB1 profile geometry/timebase')
        profile.update(index=i, offset=offset, flags=flags)
        profiles.append(profile)
    return dict(format='HVB1', bytes=len(data), profiles=profiles,
                sha256=hashlib.sha256(data).hexdigest())


def bundle(clips: list[bytes]) -> bytes:
    if len(clips) != 5:
        raise ValueError('HVB1 requires five profiles')
    offset = 112
    index = bytearray()
    for i, clip in enumerate(clips):
        index += struct.pack('<IIII', offset, len(clip), int(i != 0), 0)
        offset += len(clip)
    header = struct.pack('<4sHHIIIII', b'HVB1', 1, 32, 5, offset, zlib.crc32(index), 0, 0)
    result = header + struct.pack('<I', zlib.crc32(header)) + index + b''.join(clips)
    validate_video(result)
    return result


def prepare(source: Path, output: Path, ffmpeg: str, crf: int) -> dict:
    original = source.read_bytes()
    meta = validate_clip(original)
    if (meta['codec'], meta['width'], meta['height'], meta['fps_num'], meta['fps_den'],
            meta['frames'], meta['duration_ms']) != (1, 640, 360, 10, 1, 120, 12000):
        raise ValueError('Expected the existing 12-second 360p/10fps recorded HVC1 source')
    output.parent.mkdir(parents=True, exist_ok=True)
    annexb = output.parent / 'source_360p.h264'
    annexb.write_bytes(b''.join(payload for _, _, payload in records(original)))
    clips = [original]
    for i, (width, height) in enumerate(PROFILE_SIZES[1:], 1):
        encoded = output.parent / f'profile_{i}_{height}p.h264'
        subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-r', '10',
                        '-i', str(annexb), '-an', '-vf', f'fps=5,scale={width}:{height}:flags=lanczos',
                        '-frames:v', '60', '-pix_fmt', 'yuv420p', '-c:v', 'libx264',
                        '-preset', 'medium', '-tune', 'zerolatency', '-profile:v', 'baseline',
                        '-crf', str(crf), '-x264-params',
                        'keyint=1:min-keyint=1:scenecut=0:repeat-headers=1:aud=1',
                        '-f', 'h264', str(encoded)], check=True)
        clip, _ = make(encoded.read_bytes(), 1, width, height, 5)
        validate_clip(clip, independent=True)
        (output.parent / f'profile_{i}_{height}p.bin').write_bytes(clip)
        clips.append(clip)
    result = bundle(clips)
    temp = output.with_suffix(output.suffix + '.tmp')
    temp.write_bytes(result)
    temp.replace(output)
    manifest = validate_video(result)
    manifest.update(source=str(source.resolve()), source_sha256=hashlib.sha256(original).hexdigest(),
                    recorded_source=True, crf=crf, output=str(output.resolve()))
    output.with_suffix('.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'assets/video.bin')
    parser.add_argument('--output', type=Path, default=ROOT / '.build-range-video/video_profiles.bin')
    parser.add_argument('--ffmpeg', default=os.environ.get('FFMPEG', shutil.which('ffmpeg')))
    parser.add_argument('--crf', type=int, default=30)
    parser.add_argument('--validate', type=Path)
    args = parser.parse_args()
    try:
        if args.validate:
            result = validate_video(args.validate.read_bytes())
        else:
            if not args.ffmpeg:
                parser.error('ffmpeg is required; run inside nix-shell -p ffmpeg or pass --ffmpeg')
            if not 0 <= args.crf <= 51:
                parser.error('--crf must be 0..51')
            if args.source.resolve() == args.output.resolve():
                parser.error('Output must not overwrite the recorded source')
            result = prepare(args.source, args.output, args.ffmpeg, args.crf)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'ERROR: {error}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
