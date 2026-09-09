/* SPDX-License-Identifier: MIT */
#include "link_adapt.h"
#include "hvc_clip.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void be32(unsigned char *p,uint32_t v) { p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v; }
static void report(unsigned char *b,uint32_t stream,uint32_t stamp,unsigned received,unsigned lost) {
    memset(b,0,40);memcpy(b,"NRFB",4);b[4]=2;
    be32(b+8,1000);be32(b+16,received);be32(b+20,lost);
    unsigned loss=received+lost?1000*lost/(received+lost):0;b[24]=loss>>8;b[25]=loss;
    be32(b+32,stamp);be32(b+36,stream);
    uint32_t crc=hvc_crc32(b+8,32);b[5]=crc>>16;b[6]=crc>>8;b[7]=crc;
}
int main(void) {
    stage1_adapt_t a;unsigned char b[40];
    stage1_adapt_init(&a,50000,400000,0);stage1_adapt_stream(&a,123,0);
    assert(a.profile==0 && a.rate_bps==400000);
    const unsigned sources[]={0,1,1,1,2,3,4,4,4,4};
    const unsigned intervals[]={100,500,1000,2000,2000,2000,2000,4000,6000,10000};
    for(unsigned i=1;i<=9;i++) {
        report(b,123,i*1000,90,10);assert(stage1_adapt_feedback(&a,b,40,i*1000));
        assert(stage1_adapt_tick(&a,i*1000,-12,true,true,0,0,0));
        assert(a.profile==i && stage1_adapt_source(&a)==sources[i]);
        assert(stage1_adapt_interval_ms(&a)==intervals[i] && a.rate_bps>=50000);
    }
    stage1_adapt_congested(&a,10000);assert(a.profile==9 && a.rate_bps==50000);
    /* Ten-second deliberate gaps must not trigger a feedback timeout. */
    assert(!stage1_adapt_tick(&a,19000,-12,true,true,0,0,0));
    assert(strcmp(a.reason,"feedback_timeout"));
    /* Empty reports are liveness, not proof that a higher quality will work. */
    for(unsigned t=20000;t<40000;t+=1000) {
        report(b,123,t,0,0);assert(stage1_adapt_feedback(&a,b,40,t));
        assert(!stage1_adapt_tick(&a,t,-12,true,true,0,0,0));
    }
    for(unsigned t=40000;t<50000;t+=1000) {
        report(b,123,t,4,0);assert(stage1_adapt_feedback(&a,b,40,t));
        stage1_adapt_tick(&a,t,-12,true,true,0,0,0);
    }
    assert(a.profile==8 && a.rate_bps==75000);
    /* Validation: old stream, corruption, duplicate timestamp, and bad length. */
    report(b,122,50000,4,0);assert(!stage1_adapt_feedback(&a,b,40,50000));
    report(b,123,50000,4,0);b[28]^=1;assert(!stage1_adapt_feedback(&a,b,40,50000));
    report(b,123,49000,4,0);assert(!stage1_adapt_feedback(&a,b,40,50000));
    assert(!stage1_adapt_feedback(&a,b,39,50000));
    assert(a.rejected_feedback==4);
    stage1_adapt_stream(&a,124,50000);assert(!a.have_feedback && a.profile==8);
    report(b,123,51000,4,0);assert(!stage1_adapt_feedback(&a,b,40,51000));
    /* Radio hint cannot raise an already reduced rate; unknown RSSI is ignored. */
    stage1_adapt_init(&a,50000,400000,0);stage1_adapt_stream(&a,1,0);
    a.rate_bps=50000;
    assert(stage1_adapt_tick(&a,1000,-80,true,true,0,0,0));
    assert(a.rate_bps==50000 && a.profile==3);
    unsigned old=a.profile;
    stage1_adapt_tick(&a,2000,-127,true,true,0,0,0);assert(a.profile==old);
    /* No stale radio sample can authorize recovery or transmission decisions. */
    report(b,1,3000,4,0);assert(stage1_adapt_feedback(&a,b,40,3000));
    assert(!stage1_adapt_tick(&a,3000,-12,false,true,0,0,0));
    assert(!a.healthy_reports && !strcmp(a.reason,"stale_radio_stats"));
    stage1_adapt_tick(&a,4000,-12,true,true,5,0,0);
    assert(a.profile==4 && !strcmp(a.reason,"queue_pressure"));
    /* Sender and local monotonic clocks can wrap independently. */
    stage1_adapt_stream(&a,2,0xfffffff0U);
    report(b,2,0xfffffff0U,4,0);assert(stage1_adapt_feedback(&a,b,40,0xfffffff0U));
    report(b,2,20,4,0);assert(stage1_adapt_feedback(&a,b,40,30));
    puts("PASS: interval/quality ladder, bounded recovery, stream/CRC/freshness, clock wrap");
}
