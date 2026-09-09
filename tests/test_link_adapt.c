/* SPDX-License-Identifier: MIT */
#include "link_adapt.h"
#include "hvc_clip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void be32(unsigned char *p,uint32_t v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static void report(unsigned char *b,uint32_t stream,uint32_t stamp,unsigned received,unsigned lost,unsigned completed) {
    memset(b,0,44);memcpy(b,"NRFB",4);b[4]=3;
    be32(b+8,200);be32(b+16,received);be32(b+20,lost);
    unsigned loss=received+lost?1000*lost/(received+lost):0;b[24]=loss>>8;b[25]=loss;
    be32(b+32,stamp);be32(b+36,stream);be32(b+40,completed);
    uint32_t crc=hvc_crc32(b+8,36);b[5]=crc>>16;b[6]=crc>>8;b[7]=crc;
}
static void ack(unsigned char *b,uint32_t stream,uint32_t token) {
    memset(b,0,20);memcpy(b,"NRPA",4);b[4]=1;b[7]=20;
    be32(b+8,stream);be32(b+12,token);be32(b+16,hvc_crc32(b,16));
}
static void probe(stage1_adapt_t *a,uint32_t token,uint32_t rtt) {
    unsigned char b[20];stage1_adapt_probe_sent(a,token);ack(b,a->stream,token);
    assert(stage1_adapt_feedback(a,b,sizeof b,token+rtt));
}
static bool tick(stage1_adapt_t *a,uint32_t now) { return stage1_adapt_tick(a,now,-12,true,true,0,0,0); }
static void init(stage1_adapt_t *a,uint32_t now) {
    stage1_adapt_init(a,50000,400000,now);stage1_adapt_stream(a,123,now);
}
int main(void) {
    stage1_adapt_t a;unsigned char b[44];init(&a,1000);
    /* First bad sample acts on the next 100ms tick; quality follows 2s cadence. */
    const unsigned profiles[]={3,4,5,6,7,8,9};
    const unsigned sources[]={1,2,3,4,4,4,4};
    const unsigned intervals[]={2000,2000,2000,2000,4000,6000,10000};
    for(unsigned i=0;i<7;i++) {
        unsigned now=1100+i*300;
        report(b,123,now,90,10,0);assert(stage1_adapt_feedback(&a,b,44,now));
        assert(tick(&a,now));assert(a.profile==profiles[i]);
        assert(stage1_adapt_source(&a)==sources[i] && stage1_adapt_interval_ms(&a)==intervals[i]);
        assert(a.rate_bps>=50000);
    }
    assert(a.rate_bps==50000);
    /* Three real round trips can interrupt a 10s wait only for a 120p trial. */
    probe(&a,3100,20);probe(&a,3300,20);probe(&a,3500,20);
    assert(tick(&a,3600));assert(a.profile==6 && a.rate_bps==100000);
    stage1_adapt_stream(&a,124,3600);
    probe(&a,3700,20);probe(&a,3900,20);probe(&a,4100,20);
    report(b,124,4200,0,0,0);assert(stage1_adapt_feedback(&a,b,44,4200));
    assert(!tick(&a,4200));assert(a.profile==6); /* ACKs/empty reports cannot prove video capacity. */
    report(b,124,4400,3,0,1);assert(stage1_adapt_feedback(&a,b,44,4400));
    assert(tick(&a,4400));assert(a.profile==5 && a.rate_bps==150000);
    /* Abrupt reversal cancels the improvement without the former 10s hold. */
    report(b,124,4500,9,1,0);assert(stage1_adapt_feedback(&a,b,44,4500));
    assert(tick(&a,4500));assert(a.profile==6);
    report(b,124,4600,9,1,0);assert(stage1_adapt_feedback(&a,b,44,4600));
    assert(!tick(&a,4600));assert(a.profile==6); /* bounded lowering, no 100ms chatter */
    /* Stale cached RSSI cannot authorize any upward trial. */
    probe(&a,4700,20);probe(&a,4900,20);probe(&a,5100,20);
    report(b,124,5200,3,0,1);assert(stage1_adapt_feedback(&a,b,44,5200));
    assert(!stage1_adapt_tick(&a,5200,-12,false,true,0,0,0));
    assert(!a.have_delivery && !a.probe_streak && !strcmp(a.reason,"stale_radio_stats"));
    /* Reject wrong stream, CRC, old report timestamp, length, unsolicited/replayed ACK. */
    report(b,123,5300,4,0,1);assert(!stage1_adapt_feedback(&a,b,44,5300));
    report(b,124,5300,4,0,1);b[28]^=1;assert(!stage1_adapt_feedback(&a,b,44,5300));
    report(b,124,5200,4,0,1);assert(!stage1_adapt_feedback(&a,b,44,5300));
    assert(!stage1_adapt_feedback(&a,b,39,5300));
    ack(b,124,9999);assert(!stage1_adapt_feedback(&a,b,20,5300));
    ack(b,124,5100);assert(!stage1_adapt_feedback(&a,b,20,5300));
    assert(a.rejected_feedback==6);
    stage1_adapt_probe_sent(&a,5400);ack(b,124,5400);
    assert(!stage1_adapt_feedback(&a,b,20,6200)); /* >750ms stale round trip */
    stage1_adapt_stream(&a,125,6300);ack(b,124,5400);
    assert(!stage1_adapt_feedback(&a,b,20,6400));
    /* Probe loss responds independently of a planned 10s video gap. */
    init(&a,0);stage1_adapt_probe_sent(&a,100);assert(tick(&a,900));
    assert(a.profile==3 && !strcmp(a.reason,"probe_timeout"));
    /* Delay, queue pressure, and regulatory Duty remain distinct causes. */
    init(&a,0);probe(&a,100,550);assert(tick(&a,700));
    assert(!strcmp(a.reason,"probe_delay"));
    init(&a,0);assert(stage1_adapt_tick(&a,100,-12,true,true,4,0,0));
    assert(!strcmp(a.reason,"queue_pressure"));
    init(&a,0);assert(!stage1_adapt_tick(&a,100,-12,true,false,0,0,0));
    assert(stage1_adapt_tick(&a,200,-12,true,false,0,0,0));
    assert(!strcmp(a.reason,"duty_wait"));
    /* Unknown RSSI is ignored and a strong cached value alone never recovers. */
    init(&a,0);a.rate_bps=50000;
    assert(stage1_adapt_tick(&a,100,-80,true,true,0,0,0));
    assert(a.rate_bps==50000 && a.profile==3);
    assert(!stage1_adapt_tick(&a,200,-127,true,true,0,0,0));
    for(unsigned t=300;t<5000;t+=100)assert(!tick(&a,t));
    assert(a.profile==3);
    /* Ordered probe tokens and local/remote report clocks wrap safely. */
    init(&a,0xfffffe00U);stage1_adapt_stream(&a,2,0xfffffe00U);
    probe(&a,0xfffffff0U,30);probe(&a,0xb8U,30);
    report(b,2,0xfffffff0U,4,0,1);assert(stage1_adapt_feedback(&a,b,44,300));
    report(b,2,20,4,0,1);assert(stage1_adapt_feedback(&a,b,44,400));
    assert(a.probe_streak==2);
    /* Alternate degradation/recovery repeatedly, always bounded and current-stream. */
    init(&a,0);
    for(unsigned cycle=0;cycle<20;cycle++) {
        unsigned t=100+cycle*2000;
        report(b,a.stream,t,9,1,0);assert(stage1_adapt_feedback(&a,b,44,t));tick(&a,t);
        assert(a.profile>=3 && a.profile<=9);
        stage1_adapt_stream(&a,1000+cycle,t);
        probe(&a,t+600,20);probe(&a,t+800,20);probe(&a,t+1000,20);
        report(b,a.stream,t+1100,4,0,1);assert(stage1_adapt_feedback(&a,b,44,t+1100));
        unsigned before=a.profile;assert(tick(&a,t+1100));assert(a.profile<before);
        assert(a.rate_bps>=50000 && a.rate_bps<=400000);
    }
    /* Good tiny ACKs cannot continuously undo failures of larger video trials. */
    init(&a,0);a.profile=9;a.rate_bps=50000;
    unsigned trials=0,failures=0,lowest=0;
    for(unsigned t=100;t<=12000;t+=100) {
        if(t%200==100)probe(&a,t,20);
        if(a.trial_active){stage1_adapt_congested(&a,t+20);failures++;}
        else if(a.trial_backoff_ms)stage1_adapt_congested(&a,t+20);
        unsigned before=a.profile;
        tick(&a,t+30);
        if(before>6 && a.profile==6)trials++;
        if(a.profile==9)lowest++;
    }
    assert(failures && trials<10 && lowest>20 && a.trial_backoff_ms);
    init(&a,0);a.profile=9;
    probe(&a,100,20);probe(&a,300,20);probe(&a,500,20);assert(tick(&a,600));
    stage1_adapt_congested(&a,900);assert(a.trial_backoff_ms==500);
    stage1_adapt_stream(&a,200,900);
    probe(&a,1000,20);probe(&a,1200,20);probe(&a,1400,20);assert(tick(&a,1500));
    stage1_adapt_congested(&a,1800);assert(a.trial_backoff_ms==1000);
    stage1_adapt_stream(&a,201,1800);
    probe(&a,1900,20);probe(&a,2100,20);probe(&a,2300,20);assert(!tick(&a,2400));
    probe(&a,2500,20);probe(&a,2700,20);assert(tick(&a,2800));
    stage1_adapt_congested(&a,3100);assert(a.trial_backoff_ms==2000);
    /* All tiny probes succeed but every video datagram is lost: do not stick
     * at the initial quality indefinitely. Sparse planned gaps do not matter. */
    init(&a,0);stage1_adapt_video_sent(&a,100);
    for(unsigned t=100;t<=1100;t+=200){probe(&a,t,20);tick(&a,t+30);}
    assert(a.profile==3 && !strcmp(a.reason,"video_timeout"));
    init(&a,0);stage1_adapt_video_sent(&a,100);
    report(b,123,900,3,0,1);assert(stage1_adapt_feedback(&a,b,44,900));
    assert(!a.video_pending);assert(!tick(&a,1200));
    stage1_adapt_video_sent(&a,1300);stage1_adapt_stream(&a,124,1400);
    assert(!a.video_pending);
    puts("PASS: 100ms decision, bidirectional reversal, bounded 120p trial, complete-frame recovery, probe/CRC/freshness/wrap");
}
