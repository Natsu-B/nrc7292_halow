#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "nrc_video_packet.h"

int main(void)
{
    static const uint8_t annex_b_4[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x11,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x22,
    };
    static const uint8_t annex_b_self_contained[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x11,
        0x00, 0x00, 0x00, 0x01, 0x68, 0x22,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x33,
    };
    static const uint8_t annex_b_3_no_idr[] = {
        0x00, 0x00, 0x01, 0x67, 0x11,
        0x00, 0x00, 0x01, 0x61, 0x22,
    };
    static const uint8_t avcc_4[] = {
        0x00, 0x00, 0x00, 0x02, 0x67, 0x11,
        0x00, 0x00, 0x00, 0x02, 0x68, 0x22,
        0x00, 0x00, 0x00, 0x03, 0x65, 0x22, 0x33,
    };
    static const uint8_t avcc_2_no_idr[] = {
        0x00, 0x02, 0x67, 0x11,
        0x00, 0x03, 0x61, 0x22, 0x33,
    };
    static const uint8_t avcc_1[] = {
        0x02, 0x67, 0x11,
        0x03, 0x65, 0x22, 0x33,
    };
    static const uint8_t malformed[] = {
        0x00, 0x00, 0x00, 0x20, 0x65, 0x22,
    };

    assert(nrc_h264_frame_contains_idr(annex_b_4, sizeof(annex_b_4)));
    assert(!nrc_h264_frame_is_self_contained(annex_b_4, sizeof(annex_b_4)));
    assert(nrc_h264_frame_is_self_contained(annex_b_self_contained,
                                             sizeof(annex_b_self_contained)));
    assert(!nrc_h264_frame_contains_idr(annex_b_3_no_idr, sizeof(annex_b_3_no_idr)));
    assert(nrc_h264_frame_contains_idr(avcc_4, sizeof(avcc_4)));
    assert(nrc_h264_frame_is_self_contained(avcc_4, sizeof(avcc_4)));
    assert(!nrc_h264_frame_contains_idr(avcc_2_no_idr, sizeof(avcc_2_no_idr)));
    assert(nrc_h264_frame_contains_idr(avcc_1, sizeof(avcc_1)));
    assert(!nrc_h264_frame_contains_idr(malformed, sizeof(malformed)));
    assert(!nrc_h264_frame_is_self_contained(malformed, sizeof(malformed)));
    assert(!nrc_h264_frame_contains_idr(NULL, 0));
    assert(!nrc_h264_frame_is_self_contained(NULL, 0));

    puts("h264 framing test: ok");
    return 0;
}
