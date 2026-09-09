/* SPDX-License-Identifier: MIT */
#include "link_adapt.h"
#include "hvc_clip.h"
#include <string.h>

/* First trade update interval up to 2s, then resolution, then up to 10s. */
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
    a->max_bps=max?max:400000;a->min_bps=min?min:50000;
    if(a->min_bps>a->max_bps)a->min_bps=a->max_bps;
    a->rate_bps=a->max_bps;a->last_tick_ms=now;a->reason="initial";
}
void stage1_adapt_stream(stage1_adapt_t *a, uint32_t stream, uint32_t now) {
    a->stream=stream;a->stream_started_ms=now;a->stream_ready=true;
    a->have_feedback=a->pending_loss=a->pending_slow_probe=false;
    a->have_delivery=a->have_drops=a->have_probe=a->have_probe_ack=false;
    a->video_pending=false;
    a->probe_streak=0;memset(a->probe_pending,0,sizeof a->probe_pending);
}
void stage1_adapt_probe_sent(stage1_adapt_t *a, uint32_t now) {
    if(!a->stream_ready)return;
    unsigned i=a->probe_cursor++%8;
    a->probe_sent_ms[i]=now;a->probe_pending[i]=true;
    if(!a->have_probe)a->probe_started_ms=now;
    a->have_probe=true;
}
void stage1_adapt_video_sent(stage1_adapt_t *a, uint32_t now) {
    if(!a->stream_ready)return;
    if(!a->video_pending)a->video_sent_ms=now;
    a->video_pending=true;
}
bool stage1_adapt_feedback(stage1_adapt_t *a, const uint8_t *p, size_t n, uint32_t now) {
    if(!p || !a->stream_ready)goto reject;
    if(n==20 && !memcmp(p,"NRPA",4)) {
        if(p[4]!=1 || p[5] || be16(p+6)!=20 || be32(p+8)!=a->stream ||
           be32(p+16)!=hvc_crc32(p,16))goto reject;
        uint32_t token=be32(p+12);
        if(a->have_probe_ack && !newer(token,a->last_probe_token))goto reject;
        for(unsigned i=0;i<8;i++)if(a->probe_pending[i] && a->probe_sent_ms[i]==token) {
            uint32_t rtt=now-token;
            a->probe_pending[i]=false;
            if(rtt>750)goto reject;
            if(!a->have_probe_ack || now-a->last_probe_ack_ms>600)a->probe_streak=0;
            a->have_probe_ack=true;a->last_probe_ack_ms=now;a->last_probe_token=token;
            a->probe_rtt_ms=rtt;a->probe_acks++;
            if(rtt<=300){if(a->probe_streak<3)a->probe_streak++;}
            else {a->probe_streak=0;if(rtt>=500)a->pending_slow_probe=true;}
            return true;
        }
        goto reject;
    }
    if(!((n==40 && p[4]==2)||(n==44 && p[4]==3)) || memcmp(p,"NRFB",4) ||
       be32(p+36)!=a->stream || (hvc_crc32(p+8,n-8)&0xffffffU)!=
       ((uint32_t)p[5]<<16 | (uint32_t)p[6]<<8 | p[7]))goto reject;
    uint32_t interval=be32(p+8), received=be32(p+16), lost=be32(p+20), sender=be32(p+32);
    uint32_t completed=n==44?be32(p+40):0;
    uint16_t loss=be16(p+24);
    if(interval<100 || interval>10000 || received>65535 || lost>65535 ||
       received+lost>65535 || completed>65535 || loss>1000 ||
       loss!=(received+lost?1000U*lost/(received+lost):0) ||
       (a->have_feedback && !newer(sender,a->last_feedback_sender_ms)))goto reject;
    a->have_feedback=true;a->last_feedback_rx_ms=now;a->last_feedback_sender_ms=sender;
    a->accepted_feedback++;
    if(completed)a->video_pending=false;
    if(loss>=20){a->pending_loss=true;a->have_delivery=false;}
    else if(completed && !lost){a->have_delivery=true;a->delivery_ms=now;}
    return true;
reject:
    a->rejected_feedback++;return false;
}
static bool lower(stage1_adapt_t *a, uint32_t now, const char *reason) {
    if(a->trial_active) {
        a->trial_active=false;a->trial_failed_ms=now;
        a->trial_backoff_ms=a->trial_backoff_ms?a->trial_backoff_ms*2U:500U;
        if(a->trial_backoff_ms>2000)a->trial_backoff_ms=2000;
    }
    a->have_delivery=false;a->probe_streak=0;
    if(a->have_bad && now-a->last_bad_ms<250)return false;
    uint32_t before=a->rate_bps;uint8_t profile=a->profile;
    a->rate_bps=clamp(a,a->rate_bps/2U);
    /* The user permits a quick move to 2s. Beyond that, reduce pixels first. */
    if(a->profile<3)a->profile=3;
    else if(a->profile<9)a->profile++;
    a->reason=reason;a->last_bad_ms=now;a->have_bad=true;
    return before!=a->rate_bps || profile!=a->profile;
}
void stage1_adapt_congested(stage1_adapt_t *a, uint32_t now) { (void)lower(a,now,"frame_deadline"); }
bool stage1_adapt_tick(stage1_adapt_t *a, uint32_t now, int rssi, bool fresh,
                      bool duty, uint16_t queue, uint32_t host, uint32_t target) {
    if(now-a->last_tick_ms<100 || !a->stream_ready)return false;
    a->last_tick_ms=now;
    if(!fresh){a->have_delivery=false;a->probe_streak=0;a->reason="stale_radio_stats";return false;}
    bool dropped=a->have_drops && (newer(host,a->host_drops) || newer(target,a->target_drops));
    a->host_drops=host;a->target_drops=target;a->have_drops=true;
    bool loss=a->pending_loss,slow=a->pending_slow_probe;
    a->pending_loss=a->pending_slow_probe=false;
    a->duty_ticks=duty?0:a->duty_ticks<3?a->duty_ticks+1:3;
    if(dropped)return lower(a,now,"radio_or_host_drop");
    if(queue>=4)return lower(a,now,"queue_pressure");
    if(loss)return lower(a,now,"receiver_loss");
    if(a->duty_ticks>=2)return lower(a,now,"duty_wait");
    if(slow)return lower(a,now,"probe_delay");
    /* ponytail: RSSI is a cached radio hint, never a speed/direction estimate.
     * Require new acknowledged traffic before any recovery trial. */
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
        a->have_delivery=false;a->probe_streak=0;a->last_bad_ms=now;a->have_bad=true;
        a->reason="weak_rssi_hint";return true;
    }
    if(!duty){a->have_delivery=false;a->probe_streak=0;return false;}
    /* Enqueue success and small ACKs do not prove any video arrived. This
     * deadline starts after actual frame submission, not its planned interval. */
    if(a->video_pending && now-a->video_sent_ms>1000) {
        a->video_pending=false;return lower(a,now,"video_timeout");
    }
    if(a->have_probe && now-(a->have_probe_ack?a->last_probe_ack_ms:a->probe_started_ms)>750)
        return lower(a,now,"probe_timeout");
    /* Legacy/no-probe senders still time out, but planned video gaps are allowed. */
    uint32_t age=now-(a->have_feedback?a->last_feedback_rx_ms:a->stream_started_ms);
    if(!a->have_probe && age>2U*stage1_adapt_interval_ms(a)+5000U)
        return lower(a,now,"feedback_timeout");
    if(!a->have_probe_ack || now-a->last_probe_ack_ms>400 ||
       (a->have_bad && now-a->last_bad_ms<500))return false;
    uint32_t before=a->rate_bps;uint8_t profile=a->profile;
    if(a->have_delivery && now-a->delivery_ms<=1500) {
        a->trial_active=false;a->trial_backoff_ms=0;
    }
    if(a->profile>6 && a->probe_streak>=3 &&
       (!a->trial_backoff_ms || now-a->trial_failed_ms>=a->trial_backoff_ms)) {
        /* Tiny ACKs prove liveness only. Try a current 120p frame at 2s;
         * require its real completed-frame feedback before raising resolution. */
        a->profile=6;a->rate_bps=clamp(a,100000);a->reason="probe_video_trial";a->trial_active=true;
    } else if(a->probe_streak>=2 && a->have_delivery && now-a->delivery_ms<=1500) {
        if(a->profile>minimum_profile)a->profile--;
        a->rate_bps=clamp(a,a->rate_bps+a->rate_bps/2U);
        a->reason="delivered_frame_recovery";
    } else return false;
    if(a->rate_bps>cap)a->rate_bps=cap;
    a->probe_streak=0;a->have_delivery=false;
    return before!=a->rate_bps || profile!=a->profile;
}
