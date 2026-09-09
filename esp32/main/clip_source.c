/* SPDX-License-Identifier: MIT */
#include "clip_source.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "nrc_video_packet.h"
#include <stdbool.h>
#include <string.h>

#define PROFILE_COUNT 5U
#define BUNDLE_HEADER 32U
#define BUNDLE_ENTRY 16U
typedef struct {
    const esp_partition_t *partition;
    uint32_t offset, length, flags;
} source_t;
static source_t sources[PROFILE_COUNT];
static unsigned source_count;
static size_t largest_frame;
static uint16_t le16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int read_flash(void *ctx,uint32_t offset,void *dst,size_t n) {
    const source_t *s=ctx;
    if(offset>s->length || n>s->length-offset)return -1;
    return esp_partition_read(s->partition,s->offset+offset,dst,n)==ESP_OK?0:-1;
}
static int validate_records(source_t *s,const hvc_clip_t *clip,bool exact_length) {
    uint8_t record[16],buffer[512];uint32_t at=32;
    for(uint32_t i=0;i<clip->frame_count;i++) {
        if(read_flash(s,at,record,sizeof record))return -1;
        uint32_t size=le32(record),crc=0xffffffffU;
        if(s->flags && !(le32(record+8)&1U))return -1;
        at+=sizeof record;
        for(uint32_t done=0;done<size;) {
            size_t n=size-done;if(n>sizeof buffer)n=sizeof buffer;
            if(read_flash(s,at+done,buffer,n))return -1;
            for(size_t k=0;k<n;k++) {
                crc^=buffer[k];
                for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320U&(0U-(crc&1U)));
            }
            done+=(uint32_t)n;
        }
        if(~crc!=le32(record+12))return -1;
        at+=size;
    }
    return exact_length && at!=s->length?-1:0;
}
int clip_source_open(hvc_clip_t *clip) {
    if(!clip)return -1;
    source_count=0;largest_frame=0;
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"video");
    if(!p){ESP_LOGE("clip","video partition not found");return -1;}
    uint8_t header[BUNDLE_HEADER],index[PROFILE_COUNT*BUNDLE_ENTRY];
    if(p->size<sizeof header || esp_partition_read(p,0,header,sizeof header)!=ESP_OK)return -1;
    unsigned count=1;
    if(!memcmp(header,"HVC1",4))sources[0]=(source_t){p,0,p->size,0};
    else {
        uint32_t total=le32(header+12);
        if(memcmp(header,"HVB1",4) || le16(header+4)!=1 || le16(header+6)!=BUNDLE_HEADER ||
           le32(header+8)!=PROFILE_COUNT || le32(header+20) || le32(header+24) ||
           hvc_crc32(header,28)!=le32(header+28) || total>p->size ||
           total<BUNDLE_HEADER+sizeof index ||
           esp_partition_read(p,BUNDLE_HEADER,index,sizeof index)!=ESP_OK ||
           hvc_crc32(index,sizeof index)!=le32(header+16))return -1;
        count=PROFILE_COUNT;
        for(unsigned i=0;i<count;i++) {
            const uint8_t *e=index+i*BUNDLE_ENTRY;
            uint32_t offset=le32(e),length=le32(e+4),flags=le32(e+8);
            if(offset<BUNDLE_HEADER+sizeof index || offset>total || length<32 ||
               length>total-offset || flags!=(i?1U:0U) || le32(e+12))return -1;
            for(unsigned j=0;j<i;j++)
                if(offset<sources[j].offset+sources[j].length && sources[j].offset<offset+length)return -1;
            sources[i]=(source_t){p,offset,length,flags};
        }
    }
    static const uint16_t widths[PROFILE_COUNT]={640,640,426,320,214};
    static const uint16_t heights[PROFILE_COUNT]={360,360,240,180,120};
    uint32_t duration=0;
    for(unsigned i=0;i<count;i++) {
        hvc_clip_t check;
        if(hvc_clip_open(&check,read_flash,&sources[i],sources[i].length) ||
           validate_records(&sources[i],&check,count>1))return -1;
        if(count>1 && (check.codec!=1 || check.width!=widths[i] || check.height!=heights[i] ||
           check.fps_num!=(i?5:10) || check.fps_den!=1 || (i && check.duration_ms!=duration)))return -1;
        duration=check.duration_ms;
        if(check.max_frame>largest_frame)largest_frame=check.max_frame;
    }
    source_count=count;
    return clip_source_select(clip,0);
}
unsigned clip_source_profile_count(void) { return source_count; }
size_t clip_source_max_frame(void) { return source_count?largest_frame:0; }
int clip_source_select(hvc_clip_t *clip,unsigned profile) {
    if(!clip || profile>=source_count)return -1;
    return hvc_clip_open(clip,read_flash,&sources[profile],sources[profile].length);
}
static int latest(hvc_clip_t *clip,uint32_t pts_ms,uint8_t *buffer,size_t capacity,hvc_frame_t *frame,bool key_only) {
    if(!clip || !buffer || !frame || !clip->read)return -1;
    uint32_t at=HVC_HEADER_BYTES,chosen=0,chosen_index=0,previous=0;
    for(uint32_t i=0;i<clip->frame_count;i++) {
        uint8_t record[HVC_RECORD_BYTES];
        if(at>clip->capacity || sizeof record>clip->capacity-at ||
           clip->read(clip->context,at,record,sizeof record))return -1;
        uint32_t n=le32(record),pts=le32(record+4),flags=le32(record+8);
        if(!n || n>HVC_MAX_FRAME_BYTES || n>clip->capacity-at-sizeof record ||
           flags&~1U || pts>=clip->duration_ms || (i && pts<=previous))return -1;
        if(pts>pts_ms)break;
        if(!key_only || (flags&1U)){chosen=at;chosen_index=i;}
        previous=pts;at+=sizeof record+n;
    }
    if(!chosen)return 1;
    hvc_clip_t next=*clip;next.position=chosen;next.frame_index=chosen_index;
    int result=hvc_clip_next(&next,buffer,capacity,frame);
    if(result)return result;
    for(unsigned i=0;i<source_count;i++)
        if(next.context==&sources[i] && sources[i].flags && !(frame->flags&1U))return -1;
    if(next.codec==1 && (frame->flags&1U) && !nrc_h264_frame_is_self_contained(buffer,frame->size))return -1;
    *clip=next;return 0;
}
int clip_source_latest(hvc_clip_t *clip,uint32_t pts_ms,uint8_t *buffer,size_t capacity,hvc_frame_t *frame) {
    return latest(clip,pts_ms,buffer,capacity,frame,false);
}
int clip_source_latest_key(hvc_clip_t *clip,uint32_t pts_ms,uint8_t *buffer,size_t capacity,hvc_frame_t *frame) {
    return latest(clip,pts_ms,buffer,capacity,frame,true);
}
