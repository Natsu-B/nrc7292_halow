/* SPDX-License-Identifier: MIT */
#pragma once
#include "hvc_clip.h"
int clip_source_open(hvc_clip_t *clip);
/* Immutable per-profile contexts keep previously opened clips valid. */
unsigned clip_source_profile_count(void);
size_t clip_source_max_frame(void);
int clip_source_select(hvc_clip_t *clip, unsigned profile);
/* Load the latest frame at or before pts_ms in this cycle. Profile 0 can return
 * a dependent P-frame: caller owns dependency/IDR recovery. Profiles 1..4 are IDR.
 * Returns 1 if none exists; cursor/frame_index advance past the returned frame.
 * Does not wrap timestamps: caller owns the recorded clip's cycle clock. */
int clip_source_latest(hvc_clip_t *clip, uint32_t pts_ms, uint8_t *buffer,
                       size_t capacity, hvc_frame_t *frame);
/* Legacy HVC1 sparse-mode fallback: latest keyframe <= pts_ms, with the same
 * cursor/return contract. Avoids repeatedly selecting dependent P-frames. */
int clip_source_latest_key(hvc_clip_t *clip, uint32_t pts_ms, uint8_t *buffer,
                           size_t capacity, hvc_frame_t *frame);
