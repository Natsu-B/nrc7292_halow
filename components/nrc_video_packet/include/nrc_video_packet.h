/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRC_VIDEO_MAX_DATAGRAM_SIZE 1400U
#define NRC_VIDEO_MIN_DATAGRAM_SIZE 128U
#define NRC_VIDEO_DEFAULT_DATAGRAM_SIZE 1400U
#define NRC_VIDEO_HEADER_SIZE 36U
#define NRC_VIDEO_MAGIC 0x4E524356UL /* "NRCV" */
#define NRC_VIDEO_VERSION_LEGACY 1U
#define NRC_VIDEO_VERSION 2U
#define NRC_VIDEO_MAX_PAYLOAD_SIZE \
    (NRC_VIDEO_MAX_DATAGRAM_SIZE - NRC_VIDEO_HEADER_SIZE)

/* Compatibility aliases retained for applications built against v0.4. */
#define NRC_VIDEO_DATAGRAM_SIZE NRC_VIDEO_DEFAULT_DATAGRAM_SIZE
#define NRC_VIDEO_PAYLOAD_SIZE \
    (NRC_VIDEO_DEFAULT_DATAGRAM_SIZE - NRC_VIDEO_HEADER_SIZE)

typedef enum {
    NRC_VIDEO_CODEC_TEST = 0,
    NRC_VIDEO_CODEC_H264 = 1,
    NRC_VIDEO_CODEC_MJPEG = 2,
} nrc_video_codec_t;

enum {
    NRC_VIDEO_FLAG_KEYFRAME = 1U << 0,
    NRC_VIDEO_FLAG_FIRST = 1U << 1,
    NRC_VIDEO_FLAG_LAST = 1U << 2,
    NRC_VIDEO_FLAG_PADDED = 1U << 3,
    NRC_VIDEO_KNOWN_FLAGS = NRC_VIDEO_FLAG_KEYFRAME | NRC_VIDEO_FLAG_FIRST |
                            NRC_VIDEO_FLAG_LAST | NRC_VIDEO_FLAG_PADDED,
};

typedef struct {
    uint16_t datagram_size;
    uint8_t packet_repetitions;
} nrc_video_packet_options_t;

#define NRC_VIDEO_PACKET_OPTIONS_DEFAULT() \
    { .datagram_size = NRC_VIDEO_DEFAULT_DATAGRAM_SIZE, .packet_repetitions = 1U }

typedef struct {
    uint32_t packet_sequence;
    uint32_t stream_id;
    uint32_t frame_id;
    uint8_t scratch[NRC_VIDEO_MAX_DATAGRAM_SIZE];
} nrc_video_packetizer_t;

typedef struct {
    uint8_t version;
    nrc_video_codec_t codec;
    uint8_t flags;
    uint32_t packet_sequence;
    uint32_t stream_id;
    uint32_t frame_id;
    uint32_t timestamp_ms;
    uint32_t frame_size;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint16_t payload_length;
    uint16_t datagram_size;
} nrc_video_packet_info_t;

typedef int (*nrc_video_packet_sink_t)(const uint8_t *packet,
                                       size_t packet_size,
                                       void *user_ctx);

void nrc_video_packetizer_init(nrc_video_packetizer_t *packetizer, uint32_t stream_id);

int nrc_video_packetize_frame_ex(nrc_video_packetizer_t *packetizer,
                                 nrc_video_codec_t codec,
                                 const uint8_t *frame,
                                 size_t frame_size,
                                 uint32_t timestamp_ms,
                                 bool keyframe,
                                 const nrc_video_packet_options_t *options,
                                 nrc_video_packet_sink_t sink,
                                 void *sink_ctx);

int nrc_video_packetize_frame(nrc_video_packetizer_t *packetizer,
                              nrc_video_codec_t codec,
                              const uint8_t *frame,
                              size_t frame_size,
                              uint32_t timestamp_ms,
                              bool keyframe,
                              nrc_video_packet_sink_t sink,
                              void *sink_ctx);

int nrc_video_parse_packet(const uint8_t *packet,
                           size_t packet_size,
                           nrc_video_packet_info_t *info,
                           const uint8_t **payload);

/* Accepts Annex-B start codes and 4/2/1-byte AVCC length prefixes. */
bool nrc_h264_frame_contains_idr(const uint8_t *frame, size_t frame_size);

/* True only when one access unit carries SPS, PPS and IDR NAL units.
 * Sparse delivery must not depend on parameter sets sent minutes earlier. */
bool nrc_h264_frame_is_self_contained(const uint8_t *frame, size_t frame_size);

#ifdef __cplusplus
}
#endif
