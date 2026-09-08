/* SPDX-License-Identifier: MIT */
#include "clip_source.h"
#include "esp_partition.h"
#include "esp_log.h"
static int read_flash(void *ctx,uint32_t offset,void *dst,size_t n) {
    return esp_partition_read((const esp_partition_t *)ctx,offset,dst,n)==ESP_OK?0:-1;
}
int clip_source_open(hvc_clip_t *clip) {
    const esp_partition_t *p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,0x40,"video");
    if(!p){ESP_LOGE("clip","video partition not found");return -1;}
    int r=hvc_clip_open(clip,read_flash,(void*)p,p->size);
    if(r)ESP_LOGE("clip","Invalid video.bin. Flash assets/test_h264.bin into video partition.");
    return r;
}
