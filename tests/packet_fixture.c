/* Produce actual C packetizer output for the Node receiver cross-language test. */
#include <stdio.h>
#include <stdlib.h>
#include "hvc_clip.h"
#include "nrc_video_packet.h"
static int rd(void *p,uint32_t off,void*dst,size_t n){FILE*f=p;if(fseek(f,(long)off,SEEK_SET))return -1;return fread(dst,1,n,f)==n?0:-1;}
static int sink(const uint8_t*p,size_t n,void*ctx){FILE*f=ctx;uint8_t len[2]={(uint8_t)(n>>8),(uint8_t)n};return fwrite(len,1,2,f)==2&&fwrite(p,1,n,f)==n?0:-1;}
int main(int argc,char**argv){
    if(argc!=3)return 2;
    FILE*in=fopen(argv[1],"rb"),*out=fopen(argv[2],"wb");if(!in||!out)return 2;
    fseek(in,0,SEEK_END);long len=ftell(in);hvc_clip_t c;if(len<0||hvc_clip_open(&c,rd,in,(uint32_t)len))return 3;
    uint8_t*data=malloc(c.max_frame);if(!data)return 4;nrc_video_packetizer_t p;nrc_video_packetizer_init(&p,0x71820001);
    hvc_frame_t f;int r;
    while((r=hvc_clip_next(&c,data,c.max_frame,&f))==0)if(nrc_video_packetize_frame(&p,(nrc_video_codec_t)c.codec,data,f.size,f.pts_ms,(f.flags&1)!=0,sink,out))return 5;
    free(data);fclose(in);fclose(out);return r==1?0:6;
}
