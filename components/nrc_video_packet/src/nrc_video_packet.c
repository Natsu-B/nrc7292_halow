/* SPDX-License-Identifier: MIT */
#include "nrc_video_packet.h"

#include <limits.h>
#include <string.h>

static void put_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void put_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint16_t get_be16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static uint32_t get_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

void nrc_video_packetizer_init(nrc_video_packetizer_t *packetizer, uint32_t stream_id)
{
    if (packetizer == NULL) {
        return;
    }
    packetizer->packet_sequence = 0;
    packetizer->stream_id = stream_id;
    packetizer->frame_id = 0;
}

static bool options_valid(const nrc_video_packet_options_t *options)
{
    return options != NULL &&
           options->datagram_size >= NRC_VIDEO_MIN_DATAGRAM_SIZE &&
           options->datagram_size <= NRC_VIDEO_MAX_DATAGRAM_SIZE &&
           options->datagram_size > NRC_VIDEO_HEADER_SIZE &&
           options->packet_repetitions >= 1U &&
           options->packet_repetitions <= 4U;
}

int nrc_video_packetize_frame_ex(nrc_video_packetizer_t *packetizer,
                                 nrc_video_codec_t codec,
                                 const uint8_t *frame,
                                 size_t frame_size,
                                 uint32_t timestamp_ms,
                                 bool keyframe,
                                 const nrc_video_packet_options_t *options,
                                 nrc_video_packet_sink_t sink,
                                 void *sink_ctx)
{
    if (packetizer == NULL || frame == NULL || frame_size == 0U || sink == NULL ||
        codec < NRC_VIDEO_CODEC_TEST || codec > NRC_VIDEO_CODEC_MJPEG ||
        !options_valid(options) || frame_size > UINT32_MAX) {
        return -1;
    }

    const size_t datagram_size = options->datagram_size;
    const size_t payload_capacity = datagram_size - NRC_VIDEO_HEADER_SIZE;
    const size_t fragment_count_sz =
        (frame_size + payload_capacity - 1U) / payload_capacity;
    if (fragment_count_sz == 0U || fragment_count_sz > UINT16_MAX) {
        return -1;
    }

    const uint16_t fragment_count = (uint16_t)fragment_count_sz;
    const uint32_t frame_id = packetizer->frame_id++;
    size_t offset = 0U;

    for (uint16_t fragment_index = 0; fragment_index < fragment_count; ++fragment_index) {
        uint8_t *packet = packetizer->scratch;
        memset(packet, 0, datagram_size);

        size_t payload_length = frame_size - offset;
        if (payload_length > payload_capacity) {
            payload_length = payload_capacity;
        }

        uint8_t flags = NRC_VIDEO_FLAG_PADDED;
        if (keyframe) {
            flags |= NRC_VIDEO_FLAG_KEYFRAME;
        }
        if (fragment_index == 0U) {
            flags |= NRC_VIDEO_FLAG_FIRST;
        }
        if ((uint16_t)(fragment_index + 1U) == fragment_count) {
            flags |= NRC_VIDEO_FLAG_LAST;
        }

        put_be32(packet + 0, NRC_VIDEO_MAGIC);
        packet[4] = NRC_VIDEO_VERSION;
        packet[5] = (uint8_t)codec;
        packet[6] = flags;
        packet[7] = NRC_VIDEO_HEADER_SIZE;
        put_be32(packet + 8, packetizer->packet_sequence++);
        put_be32(packet + 12, packetizer->stream_id);
        put_be32(packet + 16, frame_id);
        put_be32(packet + 20, timestamp_ms);
        put_be32(packet + 24, (uint32_t)frame_size);
        put_be16(packet + 28, fragment_index);
        put_be16(packet + 30, fragment_count);
        put_be16(packet + 32, (uint16_t)payload_length);
        /* v2 repurposes the old reserved field to make variable datagrams
         * self-describing and reject mixed fragment sizes within one frame. */
        put_be16(packet + 34, (uint16_t)datagram_size);
        memcpy(packet + NRC_VIDEO_HEADER_SIZE, frame + offset, payload_length);

        for (uint8_t repetition = 0; repetition < options->packet_repetitions;
             ++repetition) {
            if (sink(packet, datagram_size, sink_ctx) != 0) {
                return -1;
            }
        }
        offset += payload_length;
    }

    return 0;
}

int nrc_video_packetize_frame(nrc_video_packetizer_t *packetizer,
                              nrc_video_codec_t codec,
                              const uint8_t *frame,
                              size_t frame_size,
                              uint32_t timestamp_ms,
                              bool keyframe,
                              nrc_video_packet_sink_t sink,
                              void *sink_ctx)
{
    const nrc_video_packet_options_t options = NRC_VIDEO_PACKET_OPTIONS_DEFAULT();
    return nrc_video_packetize_frame_ex(packetizer, codec, frame, frame_size,
                                        timestamp_ms, keyframe, &options,
                                        sink, sink_ctx);
}

int nrc_video_parse_packet(const uint8_t *packet,
                           size_t packet_size,
                           nrc_video_packet_info_t *info,
                           const uint8_t **payload)
{
    if (packet == NULL || info == NULL || payload == NULL ||
        packet_size < NRC_VIDEO_MIN_DATAGRAM_SIZE ||
        packet_size > NRC_VIDEO_MAX_DATAGRAM_SIZE) {
        return -1;
    }
    const uint8_t version = packet[4];
    if (get_be32(packet + 0) != NRC_VIDEO_MAGIC ||
        (version != NRC_VIDEO_VERSION_LEGACY && version != NRC_VIDEO_VERSION) ||
        packet[7] != NRC_VIDEO_HEADER_SIZE) {
        return -1;
    }

    uint16_t declared_datagram_size = get_be16(packet + 34);
    if (version == NRC_VIDEO_VERSION_LEGACY) {
        if (packet_size != NRC_VIDEO_DEFAULT_DATAGRAM_SIZE ||
            declared_datagram_size != 0U) {
            return -1;
        }
        declared_datagram_size = NRC_VIDEO_DEFAULT_DATAGRAM_SIZE;
    } else if (declared_datagram_size != packet_size) {
        return -1;
    }

    const uint32_t payload_capacity = declared_datagram_size - NRC_VIDEO_HEADER_SIZE;
    const uint16_t payload_length = get_be16(packet + 32);
    const uint16_t fragment_count = get_be16(packet + 30);
    const uint16_t fragment_index = get_be16(packet + 28);
    const uint32_t frame_size = get_be32(packet + 24);
    if (packet[5] > NRC_VIDEO_CODEC_MJPEG ||
        (packet[6] & (uint8_t)~NRC_VIDEO_KNOWN_FLAGS) != 0U ||
        payload_length == 0U || payload_length > payload_capacity ||
        frame_size == 0U || fragment_count == 0U ||
        fragment_index >= fragment_count) {
        return -1;
    }

    const uint32_t expected_fragments = (uint32_t)
        (((uint64_t)frame_size + payload_capacity - 1U) / payload_capacity);
    const uint64_t offset = (uint64_t)fragment_index * payload_capacity;
    if (expected_fragments != fragment_count || offset >= frame_size ||
        (uint64_t)payload_length > (uint64_t)frame_size - offset) {
        return -1;
    }

    const bool first = (packet[6] & NRC_VIDEO_FLAG_FIRST) != 0U;
    const bool last = (packet[6] & NRC_VIDEO_FLAG_LAST) != 0U;
    if (first != (fragment_index == 0U) ||
        last != ((uint16_t)(fragment_index + 1U) == fragment_count)) {
        return -1;
    }
    const uint32_t expected_payload = last
                                          ? frame_size - (uint32_t)offset
                                          : payload_capacity;
    if (payload_length != expected_payload) {
        return -1;
    }

    info->version = version;
    info->codec = (nrc_video_codec_t)packet[5];
    info->flags = packet[6];
    info->packet_sequence = get_be32(packet + 8);
    info->stream_id = get_be32(packet + 12);
    info->frame_id = get_be32(packet + 16);
    info->timestamp_ms = get_be32(packet + 20);
    info->frame_size = frame_size;
    info->fragment_index = fragment_index;
    info->fragment_count = fragment_count;
    info->payload_length = payload_length;
    info->datagram_size = declared_datagram_size;
    *payload = packet + NRC_VIDEO_HEADER_SIZE;
    return 0;
}

static uint32_t get_be_length(const uint8_t *src, size_t width)
{
    uint32_t value = 0;
    for (size_t i = 0; i < width; ++i) {
        value = (value << 8) | src[i];
    }
    return value;
}

enum {
    H264_NAL_FLAG_IDR = 1U << 0,
    H264_NAL_FLAG_SPS = 1U << 1,
    H264_NAL_FLAG_PPS = 1U << 2,
};

static uint8_t h264_nal_flag(uint8_t header)
{
    switch (header & 0x1FU) {
    case 5U:
        return H264_NAL_FLAG_IDR;
    case 7U:
        return H264_NAL_FLAG_SPS;
    case 8U:
        return H264_NAL_FLAG_PPS;
    default:
        return 0U;
    }
}

static uint8_t annex_b_nal_flags(const uint8_t *frame,
                                 size_t frame_size,
                                 bool *found_start_code)
{
    uint8_t flags = 0U;
    *found_start_code = false;
    for (size_t i = 0; i + 3U < frame_size; ++i) {
        size_t nal_offset = 0U;
        if (frame[i] == 0U && frame[i + 1U] == 0U && frame[i + 2U] == 1U) {
            nal_offset = i + 3U;
        } else if (i + 4U < frame_size && frame[i] == 0U && frame[i + 1U] == 0U &&
                   frame[i + 2U] == 0U && frame[i + 3U] == 1U) {
            nal_offset = i + 4U;
        }
        if (nal_offset == 0U || nal_offset >= frame_size) {
            continue;
        }
        *found_start_code = true;
        flags |= h264_nal_flag(frame[nal_offset]);
        i = nal_offset - 1U;
    }
    return flags;
}

static uint8_t avcc_nal_flags(const uint8_t *frame,
                              size_t frame_size,
                              size_t length_width,
                              bool *valid_stream)
{
    *valid_stream = false;
    if (length_width == 0U || length_width > 4U || frame_size <= length_width) {
        return 0U;
    }

    size_t offset = 0U;
    bool saw_nal = false;
    uint8_t flags = 0U;
    while (offset < frame_size) {
        if (frame_size - offset < length_width) {
            return 0U;
        }
        const uint32_t nal_length = get_be_length(frame + offset, length_width);
        offset += length_width;
        if (nal_length == 0U || (uint64_t)nal_length > (uint64_t)frame_size - offset) {
            return 0U;
        }
        saw_nal = true;
        flags |= h264_nal_flag(frame[offset]);
        offset += nal_length;
    }

    *valid_stream = saw_nal && offset == frame_size;
    return *valid_stream ? flags : 0U;
}

static uint8_t h264_frame_nal_flags(const uint8_t *frame, size_t frame_size)
{
    if (frame == NULL || frame_size < 2U) {
        return 0U;
    }

    bool found_start_code = false;
    const uint8_t annex_flags = annex_b_nal_flags(frame, frame_size, &found_start_code);
    if (found_start_code) {
        return annex_flags;
    }

    static const size_t widths[] = {4U, 2U, 1U};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        bool valid_stream = false;
        const uint8_t flags = avcc_nal_flags(frame, frame_size, widths[i], &valid_stream);
        if (valid_stream) {
            return flags;
        }
    }
    return 0U;
}

bool nrc_h264_frame_contains_idr(const uint8_t *frame, size_t frame_size)
{
    return (h264_frame_nal_flags(frame, frame_size) & H264_NAL_FLAG_IDR) != 0U;
}

bool nrc_h264_frame_is_self_contained(const uint8_t *frame, size_t frame_size)
{
    const uint8_t required = H264_NAL_FLAG_IDR | H264_NAL_FLAG_SPS | H264_NAL_FLAG_PPS;
    return (h264_frame_nal_flags(frame, frame_size) & required) == required;
}
