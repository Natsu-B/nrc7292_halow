#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nrc_video_packet.h"

typedef struct {
    uint8_t *reassembled;
    size_t capacity;
    size_t length;
    uint32_t expected_sequence;
    uint32_t frame_id;
    uint16_t fragments;
    uint16_t expected_datagram_size;
} sink_state_t;

typedef struct {
    uint8_t packet[NRC_VIDEO_MAX_DATAGRAM_SIZE];
    size_t size;
    unsigned calls;
} capture_t;

static int reassembly_sink(const uint8_t *packet, size_t packet_size, void *user_ctx)
{
    sink_state_t *state = user_ctx;
    nrc_video_packet_info_t info;
    const uint8_t *payload = NULL;
    assert(nrc_video_parse_packet(packet, packet_size, &info, &payload) == 0);
    assert(info.version == NRC_VIDEO_VERSION);
    assert(info.datagram_size == state->expected_datagram_size);
    assert(info.packet_sequence == state->expected_sequence++);
    assert(info.codec == NRC_VIDEO_CODEC_H264);
    assert((info.flags & NRC_VIDEO_FLAG_KEYFRAME) != 0);
    if (state->fragments == 0) {
        state->frame_id = info.frame_id;
        assert((info.flags & NRC_VIDEO_FLAG_FIRST) != 0);
    } else {
        assert(info.frame_id == state->frame_id);
    }
    assert(state->length + info.payload_length <= state->capacity);
    memcpy(state->reassembled + state->length, payload, info.payload_length);
    state->length += info.payload_length;
    state->fragments++;
    if (info.fragment_index + 1U == info.fragment_count) {
        assert((info.flags & NRC_VIDEO_FLAG_LAST) != 0);
    }
    return 0;
}

static int capture_sink(const uint8_t *packet, size_t packet_size, void *user_ctx)
{
    capture_t *capture = user_ctx;
    assert(packet_size <= sizeof(capture->packet));
    memcpy(capture->packet, packet, packet_size);
    capture->size = packet_size;
    capture->calls++;
    return 0;
}

typedef struct {
    uint8_t first[NRC_VIDEO_MAX_DATAGRAM_SIZE];
    size_t size;
    unsigned calls;
} repetition_state_t;

static int repetition_sink(const uint8_t *packet, size_t packet_size, void *user_ctx)
{
    repetition_state_t *state = user_ctx;
    if ((state->calls & 1U) == 0U) {
        memcpy(state->first, packet, packet_size);
        state->size = packet_size;
    } else {
        assert(state->size == packet_size);
        assert(memcmp(state->first, packet, packet_size) == 0);
    }
    state->calls++;
    return 0;
}

int main(void)
{
    const uint16_t datagram_size = 384U;
    const size_t payload_size = datagram_size - NRC_VIDEO_HEADER_SIZE;
    const size_t source_size = payload_size * 2U + 17U;
    uint8_t *source = malloc(source_size);
    uint8_t *destination = calloc(1, source_size);
    assert(source != NULL && destination != NULL);
    for (size_t i = 0; i < source_size; ++i) {
        source[i] = (uint8_t)(i * 17U + 3U);
    }

    nrc_video_packetizer_t packetizer;
    nrc_video_packetizer_init(&packetizer, 0x12345678U);
    sink_state_t state = {
        .reassembled = destination,
        .capacity = source_size,
        .expected_datagram_size = datagram_size,
    };
    const nrc_video_packet_options_t variable = {
        .datagram_size = datagram_size,
        .packet_repetitions = 1U,
    };

    assert(nrc_video_packetize_frame_ex(&packetizer,
                                        NRC_VIDEO_CODEC_H264,
                                        source,
                                        source_size,
                                        998877U,
                                        true,
                                        &variable,
                                        reassembly_sink,
                                        &state) == 0);
    assert(state.fragments == 3U);
    assert(state.length == source_size);
    assert(memcmp(source, destination, source_size) == 0);

    /* Repetition sends byte-identical copies with the same packet sequence. */
    repetition_state_t repeated = {0};
    const nrc_video_packet_options_t repeated_options = {
        .datagram_size = 256U,
        .packet_repetitions = 2U,
    };
    const uint8_t small_frame[100] = {0x65, 1, 2, 3};
    assert(nrc_video_packetize_frame_ex(&packetizer,
                                        NRC_VIDEO_CODEC_H264,
                                        small_frame,
                                        sizeof(small_frame),
                                        123U,
                                        true,
                                        &repeated_options,
                                        repetition_sink,
                                        &repeated) == 0);
    assert(repeated.calls == 2U);

    uint8_t invalid[NRC_VIDEO_DEFAULT_DATAGRAM_SIZE] = {0};
    nrc_video_packet_info_t info;
    const uint8_t *payload;
    assert(nrc_video_parse_packet(invalid, sizeof(invalid), &info, &payload) != 0);

    uint8_t one_byte = 0xAA;
    capture_t capture = {0};
    assert(nrc_video_packetize_frame(&packetizer,
                                     NRC_VIDEO_CODEC_H264,
                                     &one_byte,
                                     1,
                                     1,
                                     false,
                                     capture_sink,
                                     &capture) == 0);
    assert(capture.calls == 1U);
    assert(capture.size == NRC_VIDEO_DEFAULT_DATAGRAM_SIZE);

    uint8_t valid[NRC_VIDEO_DEFAULT_DATAGRAM_SIZE];
    memcpy(valid, capture.packet, capture.size);
    valid[6] &= (uint8_t)~NRC_VIDEO_FLAG_LAST;
    assert(nrc_video_parse_packet(valid, sizeof(valid), &info, &payload) != 0);

    memcpy(valid, capture.packet, capture.size);
    valid[6] |= 0x80U;
    assert(nrc_video_parse_packet(valid, sizeof(valid), &info, &payload) != 0);

    memcpy(valid, capture.packet, capture.size);
    valid[35] ^= 1U; /* v2 declared datagram size mismatch */
    assert(nrc_video_parse_packet(valid, sizeof(valid), &info, &payload) != 0);

    /* Legacy v1 is accepted only as fixed 1400 bytes with a zero reserved field. */
    memcpy(valid, capture.packet, capture.size);
    valid[4] = NRC_VIDEO_VERSION_LEGACY;
    valid[34] = 0;
    valid[35] = 0;
    assert(nrc_video_parse_packet(valid, sizeof(valid), &info, &payload) == 0);
    assert(info.version == NRC_VIDEO_VERSION_LEGACY);
    assert(info.datagram_size == NRC_VIDEO_DEFAULT_DATAGRAM_SIZE);

    nrc_video_packet_options_t bad = variable;
    bad.datagram_size = NRC_VIDEO_HEADER_SIZE;
    assert(nrc_video_packetize_frame_ex(&packetizer,
                                        NRC_VIDEO_CODEC_H264,
                                        &one_byte,
                                        1,
                                        1,
                                        false,
                                        &bad,
                                        capture_sink,
                                        &capture) != 0);
    bad = variable;
    bad.packet_repetitions = 0;
    assert(nrc_video_packetize_frame_ex(&packetizer,
                                        NRC_VIDEO_CODEC_H264,
                                        &one_byte,
                                        1,
                                        1,
                                        false,
                                        &bad,
                                        capture_sink,
                                        &capture) != 0);

    assert(nrc_video_packetize_frame(&packetizer,
                                     (nrc_video_codec_t)99,
                                     &one_byte,
                                     1,
                                     1,
                                     false,
                                     capture_sink,
                                     &capture) != 0);

    free(source);
    free(destination);
    puts("packetizer: ok");
    return 0;
}
