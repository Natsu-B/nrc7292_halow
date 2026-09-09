/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One replay task owns this state. PC timestamps order reports only; freshness
 * always uses the ESP's local receipt time. No radio settings are changed. */
typedef struct {
    uint32_t min_bps, max_bps, rate_bps;
    uint32_t stream, stream_started_ms, last_tick_ms, last_bad_ms;
    uint32_t last_feedback_rx_ms, last_feedback_sender_ms;
    uint32_t accepted_feedback, rejected_feedback;
    uint32_t host_drops, target_drops;
    unsigned healthy_reports, duty_ticks;
    uint8_t profile;
    bool stream_ready, have_feedback, have_drops, have_bad;
    bool pending_loss, pending_healthy;
    const char *reason;
} stage1_adapt_t;

void stage1_adapt_init(stage1_adapt_t *, uint32_t min_bps, uint32_t max_bps, uint32_t now_ms);
void stage1_adapt_stream(stage1_adapt_t *, uint32_t stream, uint32_t now_ms);
bool stage1_adapt_feedback(stage1_adapt_t *, const uint8_t *, size_t, uint32_t now_ms);
bool stage1_adapt_tick(stage1_adapt_t *, uint32_t now_ms, int rssi_dbm,
                      bool stats_fresh, bool duty_available, uint16_t queue_depth,
                      uint32_t host_drops, uint32_t target_drops);
void stage1_adapt_congested(stage1_adapt_t *, uint32_t now_ms);
unsigned stage1_adapt_source(const stage1_adapt_t *);
uint32_t stage1_adapt_interval_ms(const stage1_adapt_t *);
