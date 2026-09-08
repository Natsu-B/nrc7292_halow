/* SPDX-License-Identifier: MIT */
#include "hvc_clip.h"
#include <string.h>
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
uint32_t hvc_crc32(const void *data, size_t n) {
    const uint8_t *p=data; uint32_t c=0xffffffffU;
    for (size_t i=0;i<n;i++) { c^=p[i]; for(unsigned j=0;j<8;j++) c=(c>>1) ^ (0xedb88320U & (0U-(c&1U))); }
    return ~c;
}
static int rd(hvc_clip_t *c, uint32_t at, void *p, size_t n) {
    if (at>c->capacity || n>c->capacity-at) return -1;
    return c->read(c->context,at,p,n);
}
int hvc_clip_open(hvc_clip_t *c,hvc_read_fn read,void *context,uint32_t capacity) {
    if(!c || !read || capacity<HVC_HEADER_BYTES) return -1;
    memset(c,0,sizeof(*c)); c->read=read;c->context=context;c->capacity=capacity;
    uint8_t h[32]; if(rd(c,0,h,sizeof h)) return -1;
    if(memcmp(h,"HVC1",4) || le16(h+4)!=1 || le16(h+6)!=32 || h[9]!=0 || le16(h+14)!=0 || hvc_crc32(h,28)!=le32(h+28)) return -1;
    c->codec=h[8];c->width=le16(h+10);c->height=le16(h+12);c->fps_num=le16(h+16);c->fps_den=le16(h+18);
    c->frame_count=le32(h+20);c->duration_ms=le32(h+24);
    if((c->codec!=1&&c->codec!=2) || !c->width || !c->height || c->width>1920 || c->height>1088 || !c->fps_num || !c->fps_den || !c->frame_count || c->frame_count>HVC_MAX_FRAMES || !c->duration_ms) return -1;
    uint32_t p=32,prev=0;
    for(uint32_t i=0;i<c->frame_count;i++) {
        uint8_t r[16]; if(rd(c,p,r,sizeof r)) return -1;
        uint32_t n=le32(r),pts=le32(r+4),flags=le32(r+8);
        if(!n || n>HVC_MAX_FRAME_BYTES || (flags&~1U) || pts>=c->duration_ms || (i&&pts<=prev) || (!i && !(flags&1U))) return -1;
        p+=16; if(n>c->capacity-p) return -1;
        if(n>c->max_frame)c->max_frame=n;
        p+=n;prev=pts;
    }
    hvc_clip_rewind(c); return 0;
}
void hvc_clip_rewind(hvc_clip_t*c) { if(c){c->position=32;c->frame_index=0;} }
int hvc_clip_next(hvc_clip_t*c,void*buffer,size_t capacity,hvc_frame_t*f) {
    if(!c||!buffer||!f||!c->read)return -1;
    if(c->frame_index>=c->frame_count)return 1;
    uint8_t r[16];if(rd(c,c->position,r,16))return -1;
    uint32_t n=le32(r);
    if(!n||n>HVC_MAX_FRAME_BYTES)return -1;
    if(n>capacity)return -2;
    if(rd(c,c->position+16,buffer,n)||hvc_crc32(buffer,n)!=le32(r+12))return -1;
    f->size=n;f->pts_ms=le32(r+4);f->flags=le32(r+8);
    c->position+=16+n;c->frame_index++;return 0;
}
