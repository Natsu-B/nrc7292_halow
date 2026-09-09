/* SPDX-License-Identifier: MIT */
#include "link_adapt.h"
#include "hvc_clip.h"
#include <string.h>

/* First trade update interval up to 2s, then resolution, then up to 10s.
 * Source 0 preserves the original 360p GOP; other sources are independent IDRs. */
static const uint8_t sources[] = {0,1,1,1,2,3,4,4,4,4};
static const uint32_t intervals[] = {100,500,1000,2000,2000,2000,2000,4000,6000,10000};
unsigned stage1_adapt_source(const stage1_adapt_t *a) { return sources[a->profile]; }
uint32_t stage1_adapt_interval_ms(const stage1_adapt_t *a) { return intervals[a->profile]; }
static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]<<8 | p[1]); }
static bool newer(uint32_t a, uint32_t b) { return a!=b && (uint32_t)(a-b)<0x80000000U; }
static uint32_t clamp(const stage1_adapt_t *a, uint32_t rate) {
    return rate<a->min_bps?a->min_bps:rate>a->max_bps?a->max_bps:rate;
}
void stage1_adapt_init(stage1_adapt_t *a, uint32_t min, uint32_t max, uint32_t now) {
    memset(a,0,sizeof *a);
    a->max_bps=max?max:400000;
    a->min_bps=min?min:50000;
    if(a->min_bps>a->max_bps)a->min_bps=a->max_bps;
    a->rate_bps=a->max_bps;a->last_tick_ms=now;a->reason="initial";
}
void stage1_adapt_stream(stage1_adapt_t *a, uint32_t stream, uint32_t now) {
    a->stream=stream;a->stream_started_ms=now;a->stream_ready=true;
    a->have_feedback=false;a->pending_loss=false;a->pending_healthy=false;
    a->healthy_reports=0;a->have_drops=false;
}
bool stage1_adapt_feedback(stage1_adapt_t *a, const uint8_t *p, size_t n, uint32_t now) {
    if(!p || n!=40 || memcmp(p,"NRFB",4) || p[4]!=2 || !a->stream_ready ||
       be32(p+36)!=a->stream || (hvc_crc32(p+8,32)&0xffffffU)!=
       ((uint32_t)p[5]<<16 | (uint32_t)p[6]<<8 | p[7]))goto reject;
    uint32_t interval=be32(p+8), received=be32(p+16), lost=be32(p+20), sender=be32(p+32);
    uint16_t loss=be16(p+24);
    if(interval<100 || interval>10000 || received>65535 || lost>65535 ||
       received+lost>65535 || loss>1000 ||
       loss!=(received+lost?1000U*lost/(received+lost):0) ||
       (a->have_feedback && !newer(sender,a->last_feedback_sender_ms)))goto reject;
    a->have_feedback=true;a->last_feedback_rx_ms=now;a->last_feedback_sender_ms=sender;
    a->accepted_feedback++;
    if(loss>=20)a->pending_loss=true;
    else if(received && !lost)a->pending_healthy=true;
    return true;
reject:
    a->rejected_feedback++;return false;
}
static bool lower(stage1_adapt_t *a, uint32_t now, const char *reason) {
    a->healthy_reports=0;
    if(a->have_bad && (uint32_t)(now-a->last_bad_ms)<1000)return false;
    uint32_t before=a->rate_bps;uint8_t profile=a->profile;
    a->rate_bps=clamp(a,a->rate_bps*3U/4U);
    if(a->profile<9)a->profile++;
    a->reason=reason;a->last_bad_ms=now;a->have_bad=true;
    return before!=a->rate_bps || profile!=a->profile;
}
void stage1_adapt_congested(stage1_adapt_t *a, uint32_t now) { (void)lower(a,now,"frame_deadline"); }
bool stage1_adapt_tick(stage1_adapt_t *a, uint32_t now, int rssi, bool fresh,
                      bool duty, uint16_t queue, uint32_t host, uint32_t target) {
    if((uint32_t)(now-a->last_tick_ms)<1000 || !a->stream_ready)return false;
    a->last_tick_ms=now;
    bool dropped=a->have_drops && (newer(host,a->host_drops) || newer(target,a->target_drops));
    a->host_drops=host;a->target_drops=target;a->have_drops=true;
    bool loss=a->pending_loss,healthy=a->pending_healthy;
    a->pending_loss=a->pending_healthy=false;
    if(!fresh){a->healthy_reports=0;a->reason="stale_radio_stats";return false;}
    a->duty_ticks=duty?0:a->duty_ticks<3?a->duty_ticks+1:3;
    if(dropped)return lower(a,now,"radio_or_host_drop");
    if(queue>=4)return lower(a,now,"queue_pressure");
    if(loss)return lower(a,now,"receiver_loss");
    if(a->duty_ticks>=2)return lower(a,now,"duty_wait");
    /* ponytail: conservative RSSI hints, not a distance prediction. Calibrate
     * against field measurements; reception feedback remains necessary to recover. */
    uint32_t cap=a->max_bps;unsigned minimum_profile=0;
    if(rssi>-127 && rssi<0) {
        if(rssi<=-88){cap=50000;minimum_profile=3;}
        else if(rssi<=-80){cap=100000;minimum_profile=3;}
        else if(rssi<=-70){cap=200000;minimum_profile=1;}
    }
    cap=clamp(a,cap);
    if(a->rate_bps>cap || a->profile<minimum_profile) {
        if(a->rate_bps>cap)a->rate_bps=cap;
        if(a->profile<minimum_profile)a->profile=(uint8_t)minimum_profile;
        a->healthy_reports=0;a->reason="weak_rssi_hint";return true;
    }
    uint32_t age=now-(a->have_feedback?a->last_feedback_rx_ms:a->stream_started_ms);
    /* A planned ten-second snapshot gap is not packet loss. */
    if(age>2U*stage1_adapt_interval_ms(a)+5000U)return lower(a,now,"feedback_timeout");
    if(!duty){a->healthy_reports=0;return false;}
    if(!healthy)return false; /* Empty interval reports never authorize recovery. */
    if(++a->healthy_reports<10)return false;
    a->healthy_reports=0;
    if(a->have_bad && (uint32_t)(now-a->last_bad_ms)<10000)return false;
    uint32_t before=a->rate_bps;uint8_t profile=a->profile;
    a->rate_bps=clamp(a,a->rate_bps+25000U);
    if(a->rate_bps>cap)a->rate_bps=cap;
    if(a->profile>minimum_profile)a->profile--;
    a->reason="receiver_recovery";
    return before!=a->rate_bps || profile!=a->profile;
}
