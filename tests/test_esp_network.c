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
    const uint8_t *p=buffer;assert(length==1024&&!memcmp(p,"NRTU",4));
    assert(mock_time<200000||mock_time>=700000); /* Neither Duty blocked nor stale telemetry can transmit. */
    assert(p[4]==1&&p[5]==0&&p[6]==0&&p[7]==32);
    assert(read_be32(p+8)==0x12345678U&&read_be32(p+12)==packets&&read_be32(p+16)==100000);
    assert(read_be32(p+24)==992&&read_be32(p+28)==hvc_crc32(p+32,992));
    for(unsigned j=0;j<992;j++)assert(p[32+j]==(uint8_t)(packets*37U+j*73U));
    packets++;return (ssize_t)length;
}
int main(void){
    char *output=NULL;size_t size=0;FILE *saved=stdout;stdout=open_memstream(&output,&size);assert(stdout);
    radio=(nrc7292_handle_t)1;network_prepared=true;rf_confirmed=true;video_socket=1;
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
    free(output);puts("ESP UDP/ping: wire CRC/pattern, pacing, RF/Duty gates, async ping stack allocation and reported headroom PASS (mock)");
    return 0;
}
