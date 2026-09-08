/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define HVC_HEADER_BYTES 32U
#define HVC_RECORD_BYTES 16U
#define HVC_MAX_FRAME_BYTES (256U * 1024U)
#define HVC_MAX_FRAMES 100000U
/* All on-flash integers are little endian. See docs/PROTOCOL.md. */
typedef int (*hvc_read_fn)(void *context, uint32_t offset, void *dst, size_t length);
typedef struct {
    hvc_read_fn read;
    void *context;
    uint32_t capacity, position, frame_index, frame_count, duration_ms, max_frame;
    uint16_t width, height, fps_num, fps_den;
    uint8_t codec;
} hvc_clip_t;
typedef struct { uint32_t size, pts_ms, flags; } hvc_frame_t;
uint32_t hvc_crc32(const void *data, size_t length);
/* Return 0 for success, -1 for malformed/read error, -2 for insufficient buffer. */
int hvc_clip_open(hvc_clip_t *clip, hvc_read_fn read, void *context, uint32_t capacity);
void hvc_clip_rewind(hvc_clip_t *clip);
/* Return 1 at EOF. Payload CRC is verified before delivery. */
int hvc_clip_next(hvc_clip_t *clip, void *buffer, size_t capacity, hvc_frame_t *frame);
