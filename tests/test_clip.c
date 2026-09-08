#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hvc_clip.h"
#include "nrc_video_packet.h"
typedef struct{unsigned char *data;size_t size;} blob_t;
static int rd(void *p,uint32_t off,void *dst,size_t n){blob_t*b=p;if(off>b->size||n>b->size-off)return -1;memcpy(dst,b->data+off,n);return 0;}
static void check(const char*path,unsigned expected,unsigned codec){
    FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);assert(size>32);rewind(f);
    blob_t b={malloc((size_t)size),(size_t)size};assert(b.data);assert(fread(b.data,1,b.size,f)==b.size);fclose(f);
    hvc_clip_t c;assert(hvc_clip_open(&c,rd,&b,(uint32_t)b.size)==0);assert(c.codec==codec&&c.frame_count==expected);
    unsigned char *buffer=malloc(c.max_frame);assert(buffer);hvc_frame_t frame;
    assert(hvc_clip_next(&c,buffer,1,&frame)==-2);hvc_clip_rewind(&c);
    for(unsigned i=0;i<expected;i++){
        assert(hvc_clip_next(&c,buffer,c.max_frame,&frame)==0);
        if(codec==1&&(frame.flags&1))assert(nrc_h264_frame_is_self_contained(buffer,frame.size));
        if(codec==2)assert(buffer[0]==255&&buffer[1]==216);
    }
    assert(hvc_clip_next(&c,buffer,c.max_frame,&frame)==1);
    hvc_clip_rewind(&c);b.data[48]^=1;assert(hvc_clip_next(&c,buffer,c.max_frame,&frame)<0);b.data[48]^=1;
    b.data[28]^=1;assert(hvc_clip_open(&c,rd,&b,(uint32_t)b.size)<0);
    free(buffer);free(b.data);
}
int main(int argc,char**argv){assert(argc==4);assert(hvc_crc32("123456789",9)==0xcbf43926U);check(argv[1],120,1);check(argv[2],12,2);check(argv[3],60,1);puts("clip CRC/bounds/H264 access units PASS");return 0;}
