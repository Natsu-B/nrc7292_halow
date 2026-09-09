/* Production UDP/ping commands with virtual SDK/network APIs; no RF claim. */
#define _GNU_SOURCE
#include "../esp32/main/app_main.c"
#include <assert.h>

static int64_t mock_time;
static unsigned packets;
static bool allow_network;
static esp_ping_config_t ping_config;
static esp_ping_callbacks_t ping_callbacks;
static unsigned ping_starts,ping_deletes;
static bool mutex_held,check_disconnect_order,video_test;
static unsigned connect_calls,disconnect_calls,video_packets;
static esp_err_t connect_result=ESP_FAIL;
static bool cancel_connect;
static bool controlled_reconnect;
static uint8_t pending_feedback[40];
static bool feedback_pending,metadata_test;
static unsigned metadata_packets;
static struct sockaddr_in feedback_source;
ssize_t recvfrom(int socket,void *buffer,size_t length,int flags,struct sockaddr *address,socklen_t *address_len){
    (void)socket;assert(length>=sizeof pending_feedback&&flags==MSG_DONTWAIT);
    if(!feedback_pending){errno=EAGAIN;return -1;}
    feedback_pending=false;memcpy(buffer,pending_feedback,sizeof pending_feedback);
    assert(*address_len>=sizeof feedback_source);memcpy(address,&feedback_source,sizeof feedback_source);
    *address_len=sizeof feedback_source;return sizeof pending_feedback;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore,TickType_t timeout){
    (void)semaphore;(void)timeout;assert(!mutex_held);
    if(check_disconnect_order)assert(!atomic_load(&desired_link)&&!atomic_load(&play_requested));
    mutex_held=true;return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore){(void)semaphore;assert(mutex_held);mutex_held=false;return pdTRUE;}
esp_err_t nrc7292_connect(nrc7292_handle_t handle,TickType_t timeout){
    assert(handle==radio&&timeout==5000&&mutex_held&&atomic_load(&desired_link));
    connect_calls++;mock_time+=(int64_t)timeout*1000;
    if(cancel_connect)atomic_store(&desired_link,false); /* Explicit stop while CONNECT is in flight. */
    return connect_result;
}
esp_err_t nrc7292_disconnect(nrc7292_handle_t handle,TickType_t timeout){
    assert(handle==radio&&timeout==6000&&mutex_held);
    assert(atomic_load(&desired_link)==controlled_reconnect&&atomic_load(&play_requested)==controlled_reconnect);
    disconnect_calls++;return ESP_OK;
}
int esp_netif_get_netif_impl_index(esp_netif_t *netif){assert(netif==halow_netif);return 3;}
esp_err_t esp_ping_new_session(const esp_ping_config_t *config,const esp_ping_callbacks_t *callbacks,esp_ping_handle_t *out){
    ping_config=*config;ping_callbacks=*callbacks;*out=&ping_config;return ESP_OK;
}
esp_err_t esp_ping_start(esp_ping_handle_t handle){assert(handle==&ping_config);ping_starts++;return ESP_OK;}
esp_err_t esp_ping_get_profile(esp_ping_handle_t handle,int profile,void *out,uint32_t size){
    assert(handle==&ping_config&&size==sizeof(uint32_t));
    *(uint32_t*)out=profile==ESP_PING_PROF_DURATION?51:ping_config.count;return ESP_OK;
}
esp_err_t esp_ping_delete_session(esp_ping_handle_t handle){assert(handle==&ping_config);ping_deletes++;return ESP_OK;}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task){assert(task==NULL);return 1234;}
int64_t esp_timer_get_time(void){return mock_time;}
void vTaskDelay(TickType_t ticks){mock_time+=(int64_t)ticks*1000;}
uint32_t esp_random(void){return 0x12345678U;}
bool nrc7292_is_connected(nrc7292_handle_t handle){(void)handle;return allow_network;}
esp_err_t nrc7292_get_link_stats(nrc7292_handle_t handle,nrc7292_link_stats_t *out){
    (void)handle;memset(out,0,sizeof *out);out->hspi_ready=true;out->wifi_connected=true;
    out->radio.duty_tx_available=mock_time<200000||mock_time>=500000;
    out->radio_stats_age_ms=mock_time>=500000&&mock_time<700000?501:0;
    return ESP_OK;
}
static uint32_t read_be32(const uint8_t*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
ssize_t sendto(int socket,const void *buffer,size_t length,int flags,const struct sockaddr *address,socklen_t address_len){
    (void)socket;(void)flags;(void)address;(void)address_len;
    const uint8_t *p=buffer;
    if(metadata_test){
        assert(length==56&&!memcmp(p,"NRCS",4)&&p[4]==3&&p[7]==56);
        assert(read_be32(p+36)==adaptation.rate_bps&&read_be32(p+40)==50);
        assert(p[44]==2&&p[45]==128&&p[46]==1&&p[47]==104); /* Actual 640x360. */
        assert(read_be32(p+48)==100&&read_be32(p+52)==hvc_crc32(p,52));
        metadata_packets++;return (ssize_t)length;
    }
    if(video_test){assert(length==1400&&mock_time>=700000);video_packets++;return (ssize_t)length;}
    assert(length==1024&&!memcmp(p,"NRTU",4));
    assert(mock_time<200000||mock_time>=700000); /* Neither Duty blocked nor stale telemetry can transmit. */
    assert(p[4]==1&&p[5]==0&&p[6]==0&&p[7]==32);
    assert(read_be32(p+8)==0x12345678U&&read_be32(p+12)==packets&&read_be32(p+16)==100000);
    assert(read_be32(p+24)==992&&read_be32(p+28)==hvc_crc32(p+32,992));
    for(unsigned j=0;j<992;j++)assert(p[32+j]==(uint8_t)(packets*37U+j*73U));
    packets++;return (ssize_t)length;
}
static void test_reconnect_state(void){
    nrc7292_link_stats_t s={.hspi_ready=true,.radio_stats_age_ms=0};
    reconnect_reset();mock_time=0;connect_calls=0;
    xSemaphoreTake(network_mutex,0);
    assert(!reconnect_step_locked(0,&s)&&!connect_calls);
    atomic_store(&desired_link,true);atomic_store(&play_requested,true);
    s.radio_stats_age_ms=501;assert(!reconnect_step_locked(0,&s));
    s.radio_stats_age_ms=0;s.hspi_ready=false;assert(!reconnect_step_locked(0,&s));
    s.hspi_ready=true;
    static const unsigned backoffs[]={1000,2000,4000,8000,16000,30000,30000};
    for(unsigned i=0;i<sizeof backoffs/sizeof backoffs[0];i++) {
        assert(reconnect_step_locked(clock_ms(),&s));
        assert(connect_calls==i+1&&atomic_load(&desired_link)&&atomic_load(&play_requested));
        assert(reconnect.due_ms==clock_ms()+backoffs[i]);
        assert(!reconnect_step_locked(reconnect.due_ms-1,&s));
        mock_time=(int64_t)reconnect.due_ms*1000;
    }
    s.wifi_connected=true;assert(!reconnect_step_locked(clock_ms(),&s));
    assert(!reconnect.failures&&!reconnect.attempts); /* Recovery clears exponential backoff. */
    s.wifi_connected=false;cancel_connect=true;
    assert(reconnect_step_locked(clock_ms(),&s));
    assert(!atomic_load(&desired_link));
    unsigned before=connect_calls;mock_time+=60000000;
    assert(!reconnect_step_locked(clock_ms(),&s)&&connect_calls==before);
    cancel_connect=false;xSemaphoreGive(network_mutex);
    atomic_store(&desired_link,true);atomic_store(&play_requested,true);
    check_disconnect_order=true;assert(disconnect_network()==ESP_OK);check_disconnect_order=false;
    assert(disconnect_calls==1&&!reconnect.due_ms);
    xSemaphoreTake(network_mutex,0);
    assert(!reconnect_step_locked(clock_ms(),&s)&&connect_calls==before);
    xSemaphoreGive(network_mutex);
    assert(request_reconnect()==ESP_ERR_INVALID_STATE&&disconnect_calls==1);
    atomic_store(&desired_link,true);atomic_store(&play_requested,true);controlled_reconnect=true;
    assert(request_reconnect()==ESP_OK&&disconnect_calls==2);
    assert(atomic_load(&desired_link)&&atomic_load(&play_requested));
    xSemaphoreTake(network_mutex,0);assert(reconnect_step_locked(clock_ms(),&s));xSemaphoreGive(network_mutex);
    assert(connect_calls==before+1&&atomic_load(&desired_link)&&atomic_load(&play_requested));
    controlled_reconnect=false;atomic_store(&desired_link,false);atomic_store(&play_requested,false);
}
static void test_video_freshness(void){
    uint8_t packet[1400]={0};
    video_test=true;allow_network=true;adapt_initialized=false;adaptation.rate_bps=100000;
    atomic_store(&play_requested,true);mock_time=500000;frame_deadline=800000;pace_at=0;
    assert(packet_sink(packet,sizeof packet,NULL)==0);
    assert(video_packets==1&&mock_time==700000); /* Stale last-good telemetry cannot authorize video. */
    mock_time=500000;frame_deadline=600000;pace_at=0;
    assert(packet_sink(packet,sizeof packet,NULL)<0&&video_packets==1);
    atomic_store(&play_requested,false);video_test=false;allow_network=false;mock_time=0;
}
static void test_feedback_and_deadlines(void){
    stage1_adapt_init(&adaptation,50000,400000,0);stage1_adapt_stream(&adaptation,0x12345678,0);
    adapt_initialized=true;adapt_last_service=adapt_last_tick=0;adapt_source_rejected=0;
    pc=(struct sockaddr_in){.sin_family=AF_INET,.sin_port=htons(50000),.sin_addr={.s_addr=htonl(0xc0a80afa)}};
    memset(pending_feedback,0,sizeof pending_feedback);memcpy(pending_feedback,"NRFB",4);pending_feedback[4]=2;
    be32(pending_feedback+8,1000);be32(pending_feedback+12,1);be32(pending_feedback+16,1);
    be32(pending_feedback+32,100);be32(pending_feedback+36,0x12345678);
    uint32_t crc=hvc_crc32(pending_feedback+8,32);pending_feedback[5]=(uint8_t)(crc>>16);
    pending_feedback[6]=(uint8_t)(crc>>8);pending_feedback[7]=(uint8_t)crc;
    feedback_source=pc;feedback_source.sin_addr.s_addr=htonl(0xc0a80afb);feedback_pending=true;
    mock_time=50000;adaptation_service();assert(!adaptation.accepted_feedback&&adapt_source_rejected==1);
    feedback_source=pc;feedback_source.sin_port=htons(50001);feedback_pending=true;
    mock_time=100000;adaptation_service();assert(!adaptation.accepted_feedback&&adapt_source_rejected==2);
    feedback_source=pc;feedback_pending=true;mock_time=150000;adaptation_service();
    assert(adaptation.accepted_feedback==1&&adaptation.last_feedback_rx_ms==150);
    /* Replayed reports from the correct source are still rejected by the stream-aware parser. */
    feedback_pending=true;mock_time=200000;adaptation_service();assert(adaptation.rejected_feedback==1);
    assert(atomic_load(&adapt_rejected)==3&&atomic_load(&adapt_feedback_age)==50);
    nrc7292_link_stats_t s={.hspi_ready=true,.wifi_connected=true,.radio={.duty_tx_available=true}};
    clip.width=640;clip.height=360;atomic_store(&play_requested,true);metadata_test=true;
    network_stats(&s,200);assert(metadata_packets==1);
    s.radio_stats_age_ms=501;network_stats(&s,200);assert(metadata_packets==1);
    metadata_test=false;atomic_store(&play_requested,false);
    mock_time=30000000;adaptation_service();
    assert(adaptation.profile==0&&adaptation.rate_bps==400000); /* Intentional silence during pause is not link degradation. */
    adapt_initialized=false;
    assert(capture_deadline_us(7000,0,100)==7250000);
    assert(capture_deadline_us(7000,1,500)==7500000);
    assert(capture_deadline_us(7000,9,10000)==9000000); /* Ten-second updates do not allow ten-second-old frames. */
    assert(frame_fits_deadline(1364,50000,7000000,7250000));
    assert(!frame_fits_deadline(1365,50000,7000000,7250000)); /* Include whole padded fragments. */
    assert(!frame_fits_deadline(100,50000,8800000,9000000));
    assert(!frame_fits_deadline(100,50000,9000000,9000000));
    mock_time=0;
}
int main(void){
    char *output=NULL;size_t size=0;FILE *saved=stdout;stdout=open_memstream(&output,&size);assert(stdout);
    radio=(nrc7292_handle_t)1;network_prepared=true;rf_confirmed=true;video_socket=1;
    test_reconnect_state();test_video_freshness();test_feedback_and_deadlines();
    test_udp(100000,1);assert(!packets); /* Explicit connected state is required. */
    allow_network=true;test_udp(99999,1);assert(!packets);
    test_udp(100000,1);assert(packets==7);
    test_ping("192.168.10.250",5);
    assert(ping_starts==1&&atomic_load(&ping_running));
    /* Protect the real overflow regression: passing the SDK's default 2560
     * bytes again must fail this test, even if the application constant changes. */
    assert(ping_config.task_stack_size>=4096&&ping_config.task_stack_size==PING_TASK_STACK_BYTES);
    assert(ping_config.interface==3&&ping_config.count==5);
    test_ping("192.168.10.250",5);assert(ping_starts==1); /* Still asynchronous/busy. */
    ping_callbacks.on_ping_end(&ping_config,ping_callbacks.cb_args);
    assert(ping_deletes==1&&!atomic_load(&ping_running));
    test_ping("192.168.10.250",100);assert(ping_starts==2&&ping_config.count==100);
    ping_callbacks.on_ping_end(&ping_config,ping_callbacks.cb_args);
    assert(ping_deletes==2&&!atomic_load(&ping_running));
    test_ping("192.168.10.250",101);assert(ping_starts==2);
    fclose(stdout);stdout=saved;
    assert(strstr(output,"\"offered\":13,\"sent\":7,\"bytes\":7168"));
    assert(strstr(output,"\"duty_pauses\":1,\"duty_blocked_ms\":300,\"queue_wait_ms\":0,\"telemetry_wait_ms\":200"));
    assert(strstr(output,"\"event\":\"ping_stack\",\"configured_bytes\":5120,\"min_free_after_report_bytes\":1234"));
    free(output);puts("ESP network: reconnect backoff/cancel/native fault/stale gates, video freshness, UDP/ping stack and wire PASS (mock)");
    return 0;
}
