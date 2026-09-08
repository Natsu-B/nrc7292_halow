/* SPDX-License-Identifier: MIT
 * SX-NEWAH-EVK / NRC7292 SDK 1.7. Stage1 and future P4 use this same wire interface.
 * User IPv4/ARP/UDP packets are bridged to the ESP, not processed here.
 * The SDK's idle lwIP/netif initialization is retained for its closed-library ABI.
 */
#include "nrc_sdk.h"
#include "api_wifi.h"
#include "system_modem_api.h"
#include "driver_nrc_tx.h"
#include "hif.h"
#include "nrc_l2_protocol.h"
#include "sx_mac.h"
#include <stdio.h>
#include <string.h>

#define VIF 0
#define FRAME_POOL 8
#define CONTROL_DEPTH 8
#define CONTROL_MAX 160
#define JP_WINDOW_US 60000000U
#define JP_DURATION_US 6000000U
#define FRAME_TTL_MS 500U

typedef struct {
    uint16_t len;
    uint8_t type;
    uint16_t sequence;
    uint32_t queued_ms;
    uint8_t data[NRC_L2_MAX_ETHERNET_FRAME];
} frame_t;
typedef struct {
    uint8_t type;
    uint16_t sequence, length;
    uint8_t payload[CONTROL_MAX];
} control_t;
static QueueHandle_t q_control, q_to_radio, q_to_host, q_out, q_stats;
static QueueHandle_t q_tx_free, q_rx_free;
static frame_t tx_pool[FRAME_POOL], rx_pool[FRAME_POOL];
static nrc_l2_stream_decoder_t decoder;
static nrc_l2_wifi_config_t wifi_cfg;
static volatile bool connected, queues_ready, host_present;
static bool configured, event_registered, jp_ready;
static volatile bool transport_fault;
static uint16_t tx_sequence=1;
static volatile uint16_t stats_ms=100;
static uint8_t tx_wire[NRC_L2_MAX_MESSAGE];
static volatile uint32_t tx_drop, rx_drop, tx_hwm, rx_hwm;

int atcmd_log(const char *fmt, ...) { (void)fmt; return 0; }
static uint32_t now_ms(void) { return xTaskGetTickCount()*portTICK_PERIOD_MS; }
static uint16_t next_sequence(void) {
    taskENTER_CRITICAL();
    tx_sequence++; if(!tx_sequence)tx_sequence=1;
    uint16_t s=tx_sequence;
    taskEXIT_CRITICAL(); return s;
}
static bool send_message(uint8_t type,uint16_t seq,const void *p,uint16_t n) {
    if(transport_fault)return false;
    size_t bytes=0;
    if(nrc_l2_message_encode(type,0,seq,p,n,tx_wire,sizeof tx_wire,&bytes))return false;
    size_t at=0;
    while(at<bytes) {
        int w=_hif_write((char*)tx_wire+at,(int)(bytes-at));
        if(w>0 && (size_t)w<=bytes-at)at+=(size_t)w;
        else if(w<0 || (size_t)w>bytes-at){transport_fault=true;connected=false;return false;}
        else {
            /* Zero is backpressure, not corruption. Keep this bounded record
             * intact even if the ESP is being flashed for >2 s. Other target
             * tasks still run; stats overwrite one slot, data pools are bounded.
             * Do not reset the radio/Duty history because a host is absent. */
            vTaskDelay(1);
        }
    }
    return true;
}
static void result(uint16_t seq,int16_t status) {
    nrc_l2_command_result_t r={seq,status};
    control_t o={.type=NRC_L2_MSG_COMMAND_RESULT,.sequence=next_sequence()};
    o.length=(uint16_t)nrc_l2_command_result_encode(&r,o.payload,sizeof o.payload);
    if(xQueueSend(q_out,&o,pdMS_TO_TICKS(20))!=pdPASS)tx_drop++;
}
static void wifi_event(int vif,tWIFI_EVENT_ID event,int size,void *data) {
    (void)size;(void)data;
    if(vif!=VIF || !queues_ready)return;
    nrc_l2_wifi_event_t e={0};
    if(event==WIFI_EVT_CONNECT_SUCCESS){connected=true;e.event=NRC_L2_WIFI_EVENT_CONNECTED;}
    else if(event==WIFI_EVT_DISCONNECT){connected=false;e.event=NRC_L2_WIFI_EVENT_DISCONNECTED;}
    else return;
    control_t o={.type=NRC_L2_MSG_WIFI_EVENT,.sequence=next_sequence()};
    o.length=(uint16_t)nrc_l2_wifi_event_encode(&e,o.payload,sizeof o.payload);
    (void)xQueueSend(q_out,&o,0); /* periodic stats reconcile a dropped event */
}
static int set_jp_guard(void) {
    if(nrc_wifi_set_country(VIF,nrc_wifi_country_from_string("JP"))!=WIFI_SUCCESS)return -1;
    if(nrc_wifi_set_tx_time(128U,2000U)!=WIFI_SUCCESS)return -1;
    if(nrc_wifi_set_cca_threshold(VIF,-83)!=WIFI_SUCCESS)return -1;
    if(nrc_wifi_enable_duty_cycle(JP_WINDOW_US,JP_DURATION_US,0U)!=WIFI_SUCCESS)return -1;
    return 0;
}
static int configure_wifi(const nrc_l2_wifi_config_t *c) {
    if(!jp_ready || strcmp(c->country,"JP") || !c->duty_cycle_enabled ||
       c->duty_window_us!=JP_WINDOW_US || c->duty_duration_us!=JP_DURATION_US || c->duty_margin_us!=0 ||
       c->tx_power_dbm!=-1 || !c->rate_control)return -1;
    /* A repeated identical configuration after ESP reconnect is idempotent.
     * Changed settings require an explicit new session; Duty is NEVER reset here. */
    if(configured) {
        uint8_t old_wire[160],new_wire[160];
        size_t a=nrc_l2_wifi_config_encode(&wifi_cfg,old_wire,sizeof old_wire);
        size_t b=nrc_l2_wifi_config_encode(c,new_wire,sizeof new_wire);
        return a && a==b && memcmp(old_wire,new_wire,a)==0 ? 0 : -1;
    }
    if(connected)return -1;
    if(nrc_wifi_add_network(VIF)<0)return -1;
    if(nrc_wifi_set_rate_control(VIF,true)!=WIFI_SUCCESS)return -1;
    if(nrc_wifi_set_ssid(VIF,(char*)c->ssid)!=WIFI_SUCCESS)return -1;
    tWIFI_SECURITY sec;
    switch(c->security) {
        case NRC_L2_SECURITY_OPEN:sec=WIFI_SEC_OPEN;break;
        case NRC_L2_SECURITY_WPA3_SAE:sec=WIFI_SEC_WPA3_SAE;break;
        case NRC_L2_SECURITY_WPA3_OWE:sec=WIFI_SEC_WPA3_OWE;break;
        default:return -1; /* AP-100AH uses WPA3, not WPA2. */
    }
    if(nrc_wifi_set_security(VIF,sec,sec==WIFI_SEC_WPA3_SAE?(char*)c->password:NULL)!=WIFI_SUCCESS)return -1;
    if(sec==WIFI_SEC_WPA3_SAE && nrc_wifi_set_sae_pwe(VIF,c->sae_pwe)!=WIFI_SUCCESS)return -1;
    if(c->bssid_valid) {
        char bssid[18];
        snprintf(bssid,sizeof bssid,"%02x:%02x:%02x:%02x:%02x:%02x",c->bssid[0],c->bssid[1],c->bssid[2],c->bssid[3],c->bssid[4],c->bssid[5]);
        if(nrc_wifi_set_bssid(VIF,bssid)!=WIFI_SUCCESS)return -1;
    }
    if(c->channel_frequency_khz) {
        uint16_t freq=(uint16_t)(c->channel_frequency_khz/100U);
        tWIFI_STATUS s=c->channel_bandwidth_mhz?nrc_wifi_set_channel_freq_bw(VIF,freq,c->channel_bandwidth_mhz):nrc_wifi_set_channel_freq(VIF,freq);
        if(s!=WIFI_SUCCESS)return -1;
    }
    if(!event_registered) {
        if(nrc_wifi_register_event_handler(VIF,wifi_event)!=WIFI_SUCCESS)return -1;
        event_registered=true;
    }
    wifi_cfg=*c;stats_ms=c->stats_period_ms<50?50:(c->stats_period_ms>1000?1000:c->stats_period_ms);
    configured=true;return 0;
}
static void control_task(void *arg) {
    (void)arg;control_t c;
    for(;;) {
        if(xQueueReceive(q_control,&c,portMAX_DELAY)!=pdPASS)continue;
        switch(c.type) {
            case NRC_L2_MSG_HELLO_REQ: {
                /* SDK returns "xx:xx:xx:xx:xx:xx", NOT six bytes. */
                char mac_text[32]={0};uint8_t mac[6];
                if(nrc_wifi_get_mac_address(VIF,mac_text)!=WIFI_SUCCESS||sx_parse_mac(mac_text,mac)) {
                    result(c.sequence,NRC_L2_RESULT_IO);break;
                }
                nrc_l2_hello_t h={
                    .features=NRC_L2_FEATURE_ETHERNET_BRIDGE|NRC_L2_FEATURE_PERIODIC_LINK_STATS|NRC_L2_FEATURE_HSPI_LOOPBACK|
                     NRC_L2_FEATURE_INTERNAL_RATE_CONTROL|NRC_L2_FEATURE_HOST_OWNS_IP|
                     NRC_L2_FEATURE_PRIORITY_QUEUES|NRC_L2_FEATURE_CHANNEL_BW_CONFIG|
                     NRC_L2_FEATURE_BSSID_LOCK|NRC_L2_FEATURE_BOUNDED_CONNECT|NRC_L2_FEATURE_DUTY_CYCLE_GUARD,
                    .max_ethernet_frame=NRC_L2_MAX_ETHERNET_FRAME,.minimum_stats_period_ms=50};
                memcpy(h.mac,mac,6);host_present=true;
                control_t o={.type=NRC_L2_MSG_HELLO_RSP,.sequence=next_sequence()};
                o.length=(uint16_t)nrc_l2_hello_encode(&h,o.payload,sizeof o.payload);
                (void)xQueueSend(q_out,&o,pdMS_TO_TICKS(50));break;
            }
            case NRC_L2_MSG_WIFI_CONFIG: {
                nrc_l2_wifi_config_t w;
                if(!nrc_l2_wifi_config_decode(c.payload,c.length,&w))result(c.sequence,NRC_L2_RESULT_INVALID);
                else result(c.sequence,configure_wifi(&w)?NRC_L2_RESULT_IO:NRC_L2_RESULT_OK);
                break;
            }
            case NRC_L2_MSG_CONNECT: {
                nrc_l2_connect_request_t request={.timeout_ms=30000};
                if(c.length && !nrc_l2_connect_request_decode(c.payload,c.length,&request)){result(c.sequence,NRC_L2_RESULT_INVALID);break;}
                uint32_t ms=request.timeout_ms<1000?1000:(request.timeout_ms>60000?60000:request.timeout_ms);
                result(c.sequence,(!configured||!jp_ready||nrc_wifi_connect(VIF,ms)!=WIFI_SUCCESS)?NRC_L2_RESULT_IO:NRC_L2_RESULT_OK);break;
            }
            case NRC_L2_MSG_DISCONNECT:
                result(c.sequence,nrc_wifi_disconnect(VIF,5000)==WIFI_SUCCESS?NRC_L2_RESULT_OK:NRC_L2_RESULT_IO);break;
            default:result(c.sequence,NRC_L2_RESULT_UNSUPPORTED);break;
        }
    }
}
static void radio_tx_task(void *arg) {
    (void)arg;frame_t *f;
    for(;;) {
        if(xQueueReceive(q_to_radio,&f,portMAX_DELAY)!=pdPASS)continue;
        if(transport_fault || !connected || !jp_ready || now_ms()-f->queued_ms>FRAME_TTL_MS || !nrc_wifi_tx_avaliable_duty_cycle())tx_drop++;
        else {
            uint8_t *parts[]={f->data};uint16_t sizes[]={f->len};
            if(nrc_transmit_from_8023_mb(VIF,parts,sizes,1)!=0)tx_drop++;
        }
        (void)xQueueSend(q_tx_free,&f,0);
    }
}
static void hif_tx_task(void *arg) {
    (void)arg;control_t o;frame_t *f;
    for(;;) {
        if(transport_fault){vTaskDelay(pdMS_TO_TICKS(100));continue;}
        if(xQueueReceive(q_out,&o,0)==pdPASS || xQueueReceive(q_stats,&o,0)==pdPASS) {
            if(!send_message(o.type,o.sequence,o.payload,o.length))rx_drop++;
        } else if(xQueueReceive(q_to_host,&f,pdMS_TO_TICKS(5))==pdPASS) {
            if(f->type==NRC_L2_MSG_ETH_RX && now_ms()-f->queued_ms>FRAME_TTL_MS)rx_drop++;
            else if(!send_message(f->type,f->sequence,f->data,f->len))rx_drop++;
            (void)xQueueSend(q_rx_free,&f,0);
        }
    }
}
static void stats_task(void *arg) {
    (void)arg;
    for(;;) {
        if(host_present) {
            nrc_l2_link_stats_t s={.sample_time_ms=now_ms(),.connected=connected};
            int8_t avg=-127,last=-127;uint8_t snr=0,tx=255,rx=255,bw=0;
            if(connected) {
                (void)nrc_wifi_get_rssi(VIF,&last);(void)nrc_wifi_get_average_rssi(VIF,&avg);
                (void)nrc_wifi_get_snr(VIF,&snr);(void)nrc_wifi_get_mcs_info(VIF,&tx,&rx);
                (void)nrc_wifi_get_channel_bandwidth(VIF,&bw);
            }
            s.rssi_last_dbm=last;s.rssi_average_dbm=avg;
            s.data_rssi_last_dbm=last;s.data_rssi_average_dbm=avg;
            if(connected && system_api_get_data_rssi(VIF,&avg,&last)){s.data_rssi_average_dbm=avg;s.data_rssi_last_dbm=last;}
            s.snr_db=(int8_t)snr;s.tx_mcs=tx;s.rx_mcs=rx;s.channel_bandwidth_mhz=bw;
            s.duty_tx_available=(uint8_t)(jp_ready&&nrc_wifi_tx_avaliable_duty_cycle());
            s.modem_tx_space=system_modem_api_get_tx_space();s.modem_rx_space=system_modem_api_get_rx_space();
            s.host_to_radio_drops=tx_drop;s.radio_to_host_drops=rx_drop;
            s.host_to_radio_queue_depth=(uint16_t)uxQueueMessagesWaiting(q_to_radio);
            s.radio_to_host_queue_depth=(uint16_t)uxQueueMessagesWaiting(q_to_host);
            s.host_to_radio_queue_high_water=(uint16_t)tx_hwm;s.radio_to_host_queue_high_water=(uint16_t)rx_hwm;
            control_t o={.type=NRC_L2_MSG_LINK_STATS,.sequence=next_sequence()};
            o.length=(uint16_t)nrc_l2_link_stats_encode(&s,o.payload,sizeof o.payload);
            (void)xQueueOverwrite(q_stats,&o);
        }
        vTaskDelay(pdMS_TO_TICKS(stats_ms));
    }
}
static void decoded(const nrc_l2_message_view_t *m,void *ctx) {
    (void)ctx;
    if(m->type==NRC_L2_MSG_LOOPBACK_REQ) {
        /* Native transport bring-up does not touch the radio. Echo one complete
         * record, even when it crossed several HSPI slots/read callbacks. */
        frame_t *f=NULL;
        if(!m->payload_length || m->payload_length>NRC_L2_MAX_ETHERNET_FRAME ||
           xQueueReceive(q_rx_free,&f,0)!=pdPASS) { result(m->sequence,NRC_L2_RESULT_BUSY);return; }
        f->type=NRC_L2_MSG_LOOPBACK_RSP;f->sequence=m->sequence;
        f->len=m->payload_length;memcpy(f->data,m->payload,f->len);
        if(xQueueSend(q_to_host,&f,0)!=pdPASS) {
            (void)xQueueSend(q_rx_free,&f,0);result(m->sequence,NRC_L2_RESULT_BUSY);
        }
    } else if(m->type==NRC_L2_MSG_ETH_TX) {
        frame_t *f=NULL;
        if(m->payload_length<14 || m->payload_length>NRC_L2_MAX_ETHERNET_FRAME || !connected || xQueueReceive(q_tx_free,&f,0)!=pdPASS){tx_drop++;return;}
        f->len=m->payload_length;f->queued_ms=now_ms();memcpy(f->data,m->payload,f->len);
        if(xQueueSend(q_to_radio,&f,0)!=pdPASS){tx_drop++;(void)xQueueSend(q_tx_free,&f,0);}
        uint32_t n=uxQueueMessagesWaiting(q_to_radio);if(n>tx_hwm)tx_hwm=n;
    } else {
        if(m->payload_length>CONTROL_MAX)return;
        control_t c={.type=m->type,.sequence=m->sequence,.length=m->payload_length};
        if(c.length)memcpy(c.payload,m->payload,c.length);
        (void)xQueueSend(q_control,&c,0);
    }
}
static void hif_rx(char *data,int n) { if(!transport_fault && n>0)(void)nrc_l2_stream_decoder_feed(&decoder,(uint8_t*)data,(size_t)n,decoded,NULL); }
void l2bridge_wifi_rx(uint8_t vif,void *buffer,int n) {
    if(transport_fault || !queues_ready || vif!=VIF || !buffer || n<14 || (unsigned)n>NRC_L2_MAX_ETHERNET_FRAME)return;
    frame_t *f=NULL;
    if(xQueueReceive(q_rx_free,&f,0)!=pdPASS){rx_drop++;return;}
    f->type=NRC_L2_MSG_ETH_RX;f->sequence=next_sequence();f->queued_ms=now_ms();
    f->len=(uint16_t)n;memcpy(f->data,buffer,(size_t)n);
    if(xQueueSend(q_to_host,&f,0)!=pdPASS){rx_drop++;(void)xQueueSend(q_rx_free,&f,0);}
    uint32_t depth=uxQueueMessagesWaiting(q_to_host);if(depth>rx_hwm)rx_hwm=depth;
}
void user_init(void) {
    /* WPA is initialized by SDK nrc_user_app_main before this call. No connection here. */
    jp_ready=set_jp_guard()==0;
    if(!jp_ready){hal_uart_printf("L2BRIDGE: JP guard FAILED; HSPI diagnostics only, no association allowed\n");}
    q_control=xQueueCreate(CONTROL_DEPTH,sizeof(control_t));q_out=xQueueCreate(CONTROL_DEPTH,sizeof(control_t));
    q_to_radio=xQueueCreate(FRAME_POOL,sizeof(frame_t*));q_to_host=xQueueCreate(FRAME_POOL,sizeof(frame_t*));
    q_tx_free=xQueueCreate(FRAME_POOL,sizeof(frame_t*));q_rx_free=xQueueCreate(FRAME_POOL,sizeof(frame_t*));
    q_stats=xQueueCreate(1,sizeof(control_t));
    if(!q_control||!q_out||!q_to_radio||!q_to_host||!q_tx_free||!q_rx_free||!q_stats){hal_uart_printf("L2BRIDGE: queue allocation failed\n");return;}
    for(unsigned i=0;i<FRAME_POOL;i++){frame_t *a=&tx_pool[i],*b=&rx_pool[i];xQueueSend(q_tx_free,&a,0);xQueueSend(q_rx_free,&b,0);}
    queues_ready=true;nrc_l2_stream_decoder_init(&decoder);
    static char rxbuf[NRC_L2_MAX_MESSAGE];
    _hif_info_t h={0};h.type=_HIF_TYPE_HSPI;h.hspi.sw_id=0x000800;h.hspi.bd_id=0x07020902;
    h.rx_params.buf.addr=rxbuf;h.rx_params.buf.size=sizeof rxbuf;h.rx_params.cb=hif_rx;
    if(_hif_open(&h)!=0){hal_uart_printf("L2BRIDGE: HSPI open failed\n");return;}
    if(xTaskCreate(control_task,"l2_ctrl",2048,NULL,3,NULL)!=pdPASS ||
       xTaskCreate(radio_tx_task,"l2_radio",1024,NULL,3,NULL)!=pdPASS ||
       xTaskCreate(hif_tx_task,"l2_hif",1024,NULL,3,NULL)!=pdPASS ||
       xTaskCreate(stats_task,"l2_stats",1024,NULL,2,NULL)!=pdPASS) {
        hal_uart_printf("L2BRIDGE: task allocation failed; reset required\n");jp_ready=false;return;
    }
    hal_uart_printf("L2BRIDGE 0.8.0 ready; JP duty=60000000/6000000 us; waiting for host\n");
}
