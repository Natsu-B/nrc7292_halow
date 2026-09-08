/* SPDX-License-Identifier: MIT
 * Bench-only file replay. No camera, H.264 encoder, flight logic or radio-limit bypass.
 */
#include "sdkconfig.h"
#include "nrc7292.h"
#include "nrc_video_packet.h"
#include "clip_source.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifndef CONFIG_STAGE1_WIRING_CONFIRMED
#define CONFIG_STAGE1_WIRING_CONFIRMED 0
#endif
#ifndef CONFIG_STAGE1_RF_ENABLE
#define CONFIG_STAGE1_RF_ENABLE 0
#endif
#ifndef CONFIG_STAGE1_OPEN_NETWORK
#define CONFIG_STAGE1_OPEN_NETWORK 0
#endif
#ifndef CONFIG_STAGE1_RESET_ACTIVE_LOW
#define CONFIG_STAGE1_RESET_ACTIVE_LOW 0
#endif
static const char *TAG="stage1";
static nrc7292_handle_t radio;
static atomic_bool play_requested;
static atomic_uint sent_frames, skipped_frames;
static hvc_clip_t clip;
static int video_socket=-1;
static struct sockaddr_in pc;
static int64_t pace_at;
static int64_t frame_deadline;
static nrc_video_packetizer_t packetizer;
static uint8_t *replay_buffer;
static void pause_ms(unsigned ms){vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));}
static void be16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
/* One small network telemetry packet per second. This is additional RF traffic,
 * so it remains subject to the same native Duty guard as video. */
static void network_stats(const nrc7292_link_stats_t *s) {
    if(video_socket<0)return;
    uint8_t feedback[64];
    /* Phase1 does not change quality on feedback; drain to avoid retaining stale reports. */
    for(unsigned i=0;i<8;i++)if(recvfrom(video_socket,feedback,sizeof feedback,MSG_DONTWAIT,NULL,NULL)<0)break;
    if(!atomic_load(&play_requested)||!s->wifi_connected||!s->radio.duty_tx_available||s->host_data_queue_depth>2)return;
    uint8_t b[40]={0};memcpy(b,"NRCS",4);b[4]=1;be16(b+6,sizeof b);
    be32(b+8,(uint32_t)(esp_timer_get_time()/1000));b[12]=(uint8_t)s->radio.rssi_average_dbm;
    b[13]=(uint8_t)s->radio.snr_db;b[14]=s->radio.tx_mcs;b[15]=s->radio.rx_mcs;
    b[16]=s->radio.duty_tx_available;b[17]=s->wifi_connected;be16(b+18,s->host_data_queue_depth);
    be32(b+20,s->host_tx_queue_drops);be32(b+24,s->radio.host_to_radio_drops);
    be32(b+28,atomic_load(&sent_frames));be32(b+32,atomic_load(&skipped_frames));
    be32(b+36,hvc_crc32(b,36));
    (void)sendto(video_socket,b,sizeof b,0,(struct sockaddr*)&pc,sizeof pc);
}
static void stats_print(void) {
    nrc7292_link_stats_t s;
    if(nrc7292_get_link_stats(radio,&s)!=ESP_OK)return;
    ESP_LOGI(TAG,"link=%d hspi=%d rssi=%d mcs=%u/%u duty=%u q=%u drop=%lu/%lu frame=%u skip=%u heap=%lu",
        s.wifi_connected,s.hspi_ready,s.radio.rssi_average_dbm,s.radio.tx_mcs,s.radio.rx_mcs,s.radio.duty_tx_available,
        s.host_data_queue_depth,(unsigned long)s.host_tx_queue_drops,(unsigned long)s.radio.host_to_radio_drops,
        atomic_load(&sent_frames),atomic_load(&skipped_frames),(unsigned long)esp_get_free_heap_size());
}
static int packet_sink(const uint8_t *data,size_t size,void *ctx) {
    (void)ctx;
    for(;;) {
        if(!atomic_load(&play_requested)||!nrc7292_is_connected(radio)||esp_timer_get_time()>frame_deadline)return -1;
        nrc7292_link_stats_t s;
        if(nrc7292_get_link_stats(radio,&s)!=ESP_OK||!s.hspi_ready)return -1;
        if(s.radio.duty_tx_available && s.host_data_queue_depth<6 && esp_timer_get_time()>=pace_at)break;
        pause_ms(1);
    }
    const ssize_t n=sendto(video_socket,data,size,0,(struct sockaddr*)&pc,sizeof pc);
    if(n!=(ssize_t)size)return -1;
    pace_at=esp_timer_get_time()+(int64_t)size*8*1000000/CONFIG_STAGE1_PACE_BPS;
    return 0;
}
static void replay_task(void *arg) {
    (void)arg;
    uint8_t *frame=replay_buffer; /* allocated before publishing replay_created */
    bool active=false,wait_key=true;
    int64_t epoch=0,last_stats=0;
    uint64_t clip_offset_ms=0;
    for(;;) {
        if(esp_timer_get_time()-last_stats>=1000000){
            stats_print();nrc7292_link_stats_t s;
            if(nrc7292_get_link_stats(radio,&s)==ESP_OK)network_stats(&s);
            last_stats=esp_timer_get_time();
        }
        if(!atomic_load(&play_requested)||!nrc7292_is_connected(radio)) {
            active=false;wait_key=true;pause_ms(20);continue;
        }
        if(!active) {
            hvc_clip_rewind(&clip);clip_offset_ms=0;epoch=esp_timer_get_time();pace_at=epoch;
            nrc_video_packetizer_init(&packetizer,esp_random());active=true;wait_key=true;
        }
        hvc_frame_t f;
        int r=hvc_clip_next(&clip,frame,clip.max_frame,&f);
        if(r==1){hvc_clip_rewind(&clip);clip_offset_ms+=clip.duration_ms;continue;}
        if(r){ESP_LOGE(TAG,"Clip CRC/read error; playback stopped");atomic_store(&play_requested,false);continue;}
        int64_t due=epoch+(int64_t)(clip_offset_ms+f.pts_ms)*1000;
        while(esp_timer_get_time()<due && atomic_load(&play_requested)&&nrc7292_is_connected(radio))pause_ms(1);
        if(!atomic_load(&play_requested))continue;
        bool key=(f.flags&1U)!=0;
        if(esp_timer_get_time()>due+(int64_t)CONFIG_STAGE1_FRAME_LATE_MS*1000) {
            atomic_fetch_add(&skipped_frames,1);wait_key=true;continue;
        }
        if(clip.codec==NRC_VIDEO_CODEC_H264 && wait_key && !key){atomic_fetch_add(&skipped_frames,1);continue;}
        if(clip.codec==NRC_VIDEO_CODEC_H264 && key && !nrc_h264_frame_is_self_contained(frame,f.size)) {
            ESP_LOGE(TAG,"IDR missing SPS/PPS; invalid replay clip");atomic_store(&play_requested,false);continue;
        }
        /* A partially emitted reference frame invalidates following P frames.
         * Resume only at the next self-contained IDR. Never retransmit old video indefinitely. */
        frame_deadline=due+(int64_t)CONFIG_STAGE1_FRAME_LATE_MS*1000;
        int sent=nrc_video_packetize_frame(&packetizer,(nrc_video_codec_t)clip.codec,frame,f.size,
                                         (uint32_t)(esp_timer_get_time()/1000),key,packet_sink,NULL);
        if(sent==0){wait_key=false;atomic_fetch_add(&sent_frames,1);}
        else{wait_key=true;atomic_fetch_add(&skipped_frames,1);}
    }
}
static bool wiring_confirmed=CONFIG_STAGE1_WIRING_CONFIRMED;
static bool rf_confirmed=CONFIG_STAGE1_RF_ENABLE;
static bool network_prepared, replay_created;

static void show_pins(void) {
    printf("SPI master: CLK=%d MOSI=%d MISO=%d CS=%d EIRQ=%d RST=%d clock=%d mode=0\n",
        CONFIG_STAGE1_SPI_SCLK,CONFIG_STAGE1_SPI_MOSI,CONFIG_STAGE1_SPI_MISO,
        CONFIG_STAGE1_SPI_CS,CONFIG_STAGE1_SPI_IRQ,CONFIG_STAGE1_RESET_GPIO,CONFIG_STAGE1_SPI_CLOCK);
    puts("SX-NEWAH: native HSPI slave; RST at module is active HIGH. Check EVK path.");
}
static void diagnostics_print(void) {
    if(!radio){puts("{\"event\":\"diag\",\"opened\":false}");return;}
    nrc7292_diagnostics_t d;
    if(nrc7292_get_diagnostics(radio,&d)!=ESP_OK)return;
    printf("{\"event\":\"diag\",\"chip_id\":%u,\"sw_id\":%lu,\"board_id\":%lu,"
           "\"tx_slots\":%lu,\"rx_slots\":%lu,\"spi_errors\":%lu,\"bad_ack\":%lu,"
           "\"bad_header\":%lu,\"seq_gaps\":%lu,\"invalid_status\":%lu,\"irq_count\":%lu,"
           "\"faulted\":%d,\"rx_slot_size\":%u,\"tx_slot_size\":%u}\n",
        d.native.chip_id,(unsigned long)d.native.sw_id,(unsigned long)d.native.board_id,
        (unsigned long)d.native.tx_slots,(unsigned long)d.native.rx_slots,
        (unsigned long)d.native.spi_errors,(unsigned long)d.native.bad_ack,
        (unsigned long)d.native.bad_header,(unsigned long)d.native.sequence_gaps,
        (unsigned long)d.native.invalid_status,(unsigned long)d.irq_count,d.native.faulted,
        d.queues.target_to_host_slot_size,d.queues.host_to_target_slot_size);
}
static esp_err_t probe(void) {
    if(!wiring_confirmed){puts("Verify displayed pins/VDDIO/GND, then type wiring-confirm.");return ESP_ERR_INVALID_STATE;}
    if(radio) {
        nrc7292_link_stats_t s;
        nrc7292_get_link_stats(radio,&s);
        if(s.hspi_ready){diagnostics_print();return ESP_OK;}
        puts("Existing session faulted. Inspect diagnostics; reset/restart after stopping test. No automatic SX reset.");
        return ESP_ERR_INVALID_STATE;
    }
    nrc7292_config_t cfg=NRC7292_CONFIG_DEFAULT();
    cfg.pin_sclk=CONFIG_STAGE1_SPI_SCLK;cfg.pin_mosi=CONFIG_STAGE1_SPI_MOSI;cfg.pin_miso=CONFIG_STAGE1_SPI_MISO;
    cfg.pin_cs=CONFIG_STAGE1_SPI_CS;cfg.pin_eirq=CONFIG_STAGE1_SPI_IRQ;cfg.pin_reset=CONFIG_STAGE1_RESET_GPIO;
    cfg.reset_active_low=CONFIG_STAGE1_RESET_ACTIVE_LOW;cfg.spi_clock_hz=CONFIG_STAGE1_SPI_CLOCK;
    cfg.eirq_mode=cfg.pin_eirq<0?NRC7292_EIRQ_DISABLED:NRC7292_EIRQ_ACTIVE_HIGH_LEVEL;
    cfg.data_queue_depth=8;
    esp_err_t err=nrc7292_init(&cfg,&radio);
    if(err!=ESP_OK)return err;
    err=nrc7292_start(radio,pdMS_TO_TICKS(15000));
    diagnostics_print();
    if(err!=ESP_OK) {
        puts("HSPI/HELLO failed. Check SA switches, power, native SPI pins and matching L2BRIDGE_HSPI firmware.");
        if(nrc7292_stop(radio)==ESP_OK)radio=NULL;
    }
    return err;
}
static esp_err_t prepare_network(void) {
    if(network_prepared)return ESP_OK;
    if(!rf_confirmed){puts("RF disabled: verify JP configuration/antenna/BDF, then type rf-confirm.");return ESP_ERR_INVALID_STATE;}
    if(!CONFIG_STAGE1_OPEN_NETWORK && strlen(CONFIG_STAGE1_PASSWORD)<8) {
        puts("Set WPA3-SAE credentials in menuconfig, or explicitly choose isolated OPEN bench mode.");
        return ESP_ERR_INVALID_ARG;
    }
    esp_netif_t *netif=NULL;
    esp_err_t err=nrc7292_create_netif(radio,&netif);if(err!=ESP_OK)return err;
    esp_netif_ip_info_t ip={0};
    if(esp_netif_str_to_ip4(CONFIG_STAGE1_IP,&ip.ip)!=ESP_OK ||
       esp_netif_str_to_ip4(CONFIG_STAGE1_NETMASK,&ip.netmask)!=ESP_OK ||
       esp_netif_str_to_ip4(CONFIG_STAGE1_GATEWAY,&ip.gw)!=ESP_OK)return ESP_ERR_INVALID_ARG;
    err=esp_netif_set_ip_info(netif,&ip);if(err!=ESP_OK)return err;
    nrc7292_wifi_config_t wifi={0};memcpy(wifi.country,"JP",3);
    wifi.security=CONFIG_STAGE1_OPEN_NETWORK?NRC_L2_SECURITY_OPEN:NRC_L2_SECURITY_WPA3_SAE;
    wifi.rate_control=true;wifi.tx_power_dbm=-1;wifi.stats_period_ms=100;wifi.sae_pwe=2;
    snprintf(wifi.ssid,sizeof wifi.ssid,"%s",CONFIG_STAGE1_SSID);
    snprintf(wifi.password,sizeof wifi.password,"%s",CONFIG_STAGE1_PASSWORD);
    wifi.duty_cycle_enabled=true;wifi.duty_window_us=60000000U;wifi.duty_duration_us=6000000U;wifi.duty_margin_us=0;
    err=nrc7292_configure_wifi(radio,&wifi,pdMS_TO_TICKS(15000));if(err!=ESP_OK)return err;
    pc=(struct sockaddr_in){.sin_family=AF_INET,.sin_port=htons(CONFIG_STAGE1_UDP_PORT)};
    if(inet_pton(AF_INET,CONFIG_STAGE1_PC_IP,&pc.sin_addr)!=1)return ESP_ERR_INVALID_ARG;
    video_socket=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(video_socket<0)return ESP_FAIL;
    struct sockaddr_in local={.sin_family=AF_INET,.sin_port=htons(50001)};
    inet_pton(AF_INET,CONFIG_STAGE1_IP,&local.sin_addr);
    if(bind(video_socket,(struct sockaddr*)&local,sizeof local)<0) {
        close(video_socket);video_socket=-1;return ESP_FAIL;
    }
    struct timeval tv={.tv_sec=0,.tv_usec=100000};
    if(setsockopt(video_socket,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv)<0) {
        close(video_socket);video_socket=-1;return ESP_FAIL;
    }
    network_prepared=true;
    return ESP_OK;
}
static void test_loopback(unsigned iterations,unsigned size) {
    if(!radio || atomic_load(&play_requested) || iterations<1 || iterations>10000 || size<1 || size>1518) {
        puts("Usage after probe (pause video first): loopback [1..10000 iterations] [1..1518 bytes]");return;
    }
    uint8_t *data=malloc(size);if(!data){puts("No memory");return;}
    unsigned passed=0;esp_err_t err=ESP_OK;
    int64_t begin=esp_timer_get_time();
    for(unsigned i=0;i<iterations;i++) {
        /* Unique binary patterns include NUL/CR/LF and native framing bytes. */
        for(unsigned j=0;j<size;j++)data[j]=(uint8_t)((j*73U+i*37U)^(i>>8));
        err=nrc7292_loopback(radio,data,size,pdMS_TO_TICKS(3000));
        if(err!=ESP_OK)break;
        ++passed;
    }
    free(data);
    printf("{\"event\":\"loopback\",\"requested\":%u,\"passed\":%u,\"bytes\":%u,\"elapsed_ms\":%lu,\"error\":%d}\n",
        iterations,passed,size,(unsigned long)((esp_timer_get_time()-begin)/1000),err);
    diagnostics_print();
}
static void help(void) {
    puts("Commands: pins | wiring-confirm | probe | diag | loopback [count] [bytes]");
    puts("          rf-confirm | connect | start | pause | disconnect | status | help");
    puts("Boot does NOT drive SPI or associate. Confirmations are RAM-only; no erase/auto-flash.");
}
void app_main(void) {
    ESP_LOGI(TAG,"HaLow Stage1 0.8.0 native HSPI; target=%s",CONFIG_IDF_TARGET);
    /* A missing/bad clip must not prevent hardware diagnostics. */
    esp_err_t err=nvs_flash_init();
    if(err!=ESP_OK){ESP_LOGE(TAG,"NVS init failed: %s; no automatic erase",esp_err_to_name(err));return;}
    ESP_ERROR_CHECK(esp_netif_init());ESP_ERROR_CHECK(esp_event_loop_create_default());
    setvbuf(stdout,NULL,_IONBF,0);show_pins();help();
    char line[128];size_t used=0;bool overflow=false;
    for(;;) {
        int ch=getchar();
        if(ch==EOF){clearerr(stdin);pause_ms(20);continue;}
        if(ch!='\r' && ch!='\n') {
            if(used+1<sizeof(line))line[used++]=(char)ch;else overflow=true;
            continue;
        }
        if(overflow){used=0;overflow=false;puts("Command too long");continue;}
        if(!used)continue;
        line[used]=0;used=0;
        if(!strcmp(line,"help"))help();
        else if(!strcmp(line,"pins"))show_pins();
        else if(!strcmp(line,"wiring-confirm")){wiring_confirmed=true;puts("Wiring acknowledged for this boot. Type probe for native HSPI/HELLO.");}
        else if(!strcmp(line,"rf-confirm")){rf_confirmed=true;puts("RF acknowledged for this boot. Duty/LBT remains enforced; no association yet.");}
        else if(!strcmp(line,"probe")){
            err=probe();printf("{\"event\":\"probe\",\"error\":%d}\n",err);
        } else if(!strcmp(line,"diag"))diagnostics_print();
        else if(!strcmp(line,"status")){stats_print();diagnostics_print();}
        else if(!strcmp(line,"loopback") || !strncmp(line,"loopback ",9)){
            unsigned count=1000,size=1518;char extra;
            int parsed=sscanf(line,"loopback %u %u %c",&count,&size,&extra);
            if(parsed>2)puts("Invalid loopback arguments");else test_loopback(count,size);
        } else if(!strcmp(line,"start") || !strcmp(line,"connect")) {
            bool play=!strcmp(line,"start");
            if(play && !replay_created) {
                if(clip_source_open(&clip)){puts("Clip unavailable; probe/loopback/connect still work.");continue;}
                ESP_LOGI(TAG,"clip codec=%u %ux%u frames=%lu max=%lu bytes",clip.codec,clip.width,clip.height,
                    (unsigned long)clip.frame_count,(unsigned long)clip.max_frame);
            }
            err=probe();if(err==ESP_OK)err=prepare_network();
            if(err==ESP_OK && !nrc7292_is_connected(radio))err=nrc7292_connect(radio,pdMS_TO_TICKS(30000));
            if(err==ESP_OK && play && !replay_created) {
                replay_buffer=malloc(clip.max_frame);
                if(!replay_buffer)err=ESP_ERR_NO_MEM;
                else if(xTaskCreate(replay_task,"clip_replay",6144,NULL,5,NULL)!=pdPASS) {
                    free(replay_buffer);replay_buffer=NULL;err=ESP_ERR_NO_MEM;
                } else replay_created=true;
            }
            if(err==ESP_OK && play)atomic_store(&play_requested,true);
            printf("{\"event\":\"%s\",\"error\":%d}\n",play?"start":"connect",err);
        } else if(!strcmp(line,"pause")){atomic_store(&play_requested,false);puts("Playback paused; association and Duty history retained.");}
        else if(!strcmp(line,"disconnect")) {
            atomic_store(&play_requested,false);
            err=radio?nrc7292_disconnect(radio,pdMS_TO_TICKS(6000)):ESP_ERR_INVALID_STATE;
            printf("{\"event\":\"disconnect\",\"error\":%d}\n",err);
        } else if(line[0])help();
    }
}
