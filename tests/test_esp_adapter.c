/* SPDX-License-Identifier: MIT
 * Executes the production ESP adapter with a pthread FreeRTOS shim and a
 * register/slot-level virtual NRC7292. This is NOT an ESP-IDF or RF test.
 */
#define _POSIX_C_SOURCE 200809L
#define main reference_transport_smoke_test
#include "test_hspi_transport.c"
#undef main
#include "nrc7292.h"
#include "stub_common.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

typedef struct { pthread_mutex_t mutex; } sim_sem;
typedef struct { pthread_mutex_t mutex; pthread_cond_t cond; EventBits_t bits; } sim_event;
typedef struct {
    pthread_mutex_t mutex; pthread_cond_t cond;
    size_t cap, item_size, head, count; unsigned char data[];
} sim_queue;
typedef struct {
    pthread_t thread; pthread_mutex_t mutex; pthread_cond_t cond;
    void (*fn)(void*); void *arg; unsigned notifications;
} sim_task;
static _Thread_local sim_task *current_task;
static sim_task *tasks[16]; static unsigned task_count;
static struct timespec time_after_ms(uint64_t ms) {
    struct timespec ts; assert(!clock_gettime(CLOCK_REALTIME,&ts));
    ts.tv_sec+=(time_t)(ms/1000);ts.tv_nsec+=(long)((ms%1000)*1000000);
    if(ts.tv_nsec>=1000000000){ts.tv_sec++;ts.tv_nsec-=1000000000;}return ts;
}
int64_t esp_timer_get_time(void) {
    struct timespec ts;assert(!clock_gettime(CLOCK_MONOTONIC,&ts));
    return (int64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
}
void vTaskDelay(TickType_t ticks) {
    struct timespec ts={.tv_sec=ticks/1000,.tv_nsec=(long)(ticks%1000)*1000000};
    while(nanosleep(&ts,&ts)&&errno==EINTR){}
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    sim_sem *s=calloc(1,sizeof(*s));assert(s);pthread_mutex_init(&s->mutex,NULL);return s;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t h,TickType_t timeout) {
    sim_sem *s=h;int e;
    if(timeout==portMAX_DELAY)e=pthread_mutex_lock(&s->mutex);
    else if(!timeout)e=pthread_mutex_trylock(&s->mutex);
    else {struct timespec ts=time_after_ms(timeout);e=pthread_mutex_timedlock(&s->mutex,&ts);}
    return !e?pdTRUE:pdFALSE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {sim_sem*s=h;assert(!pthread_mutex_unlock(&s->mutex));return pdTRUE;}
void vSemaphoreDelete(SemaphoreHandle_t h) {sim_sem*s=h;pthread_mutex_destroy(&s->mutex);free(s);}
EventGroupHandle_t xEventGroupCreate(void) {
    sim_event *e=calloc(1,sizeof(*e));assert(e);pthread_mutex_init(&e->mutex,NULL);pthread_cond_init(&e->cond,NULL);return e;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t h,EventBits_t bits) {
    sim_event*e=h;pthread_mutex_lock(&e->mutex);e->bits|=bits;EventBits_t b=e->bits;
    pthread_cond_broadcast(&e->cond);pthread_mutex_unlock(&e->mutex);return b;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t h,EventBits_t bits) {
    sim_event*e=h;pthread_mutex_lock(&e->mutex);EventBits_t b=e->bits;e->bits&=~bits;pthread_mutex_unlock(&e->mutex);return b;
}
EventBits_t xEventGroupGetBits(EventGroupHandle_t h) {return xEventGroupClearBits(h,0);}
EventBits_t xEventGroupWaitBits(EventGroupHandle_t h,EventBits_t bits,BaseType_t clear,BaseType_t all,TickType_t timeout) {
    sim_event*e=h;struct timespec end=time_after_ms(timeout);pthread_mutex_lock(&e->mutex);
    for(;;){
        bool satisfied=all?(e->bits&bits)==bits:(e->bits&bits)!=0;
        if(satisfied||!timeout)break;
        int r=timeout==portMAX_DELAY?pthread_cond_wait(&e->cond,&e->mutex):pthread_cond_timedwait(&e->cond,&e->mutex,&end);
        if(r==ETIMEDOUT)break;assert(!r);
    }
    EventBits_t b=e->bits;
    if(clear && (all?(b&bits)==bits:(b&bits)!=0))e->bits&=~bits;
    pthread_mutex_unlock(&e->mutex);return b;
}
void vEventGroupDelete(EventGroupHandle_t h) {sim_event*e=h;pthread_cond_destroy(&e->cond);pthread_mutex_destroy(&e->mutex);free(e);}
QueueHandle_t xQueueCreate(UBaseType_t cap,UBaseType_t item) {
    sim_queue*q=calloc(1,sizeof(*q)+cap*item);assert(q);q->cap=cap;q->item_size=item;
    pthread_mutex_init(&q->mutex,NULL);pthread_cond_init(&q->cond,NULL);return q;
}
BaseType_t xQueueSendToBack(QueueHandle_t h,const void *item,TickType_t timeout) {
    sim_queue*q=h;struct timespec end=time_after_ms(timeout);pthread_mutex_lock(&q->mutex);
    while(q->count==q->cap && timeout){
        int r=timeout==portMAX_DELAY?pthread_cond_wait(&q->cond,&q->mutex):pthread_cond_timedwait(&q->cond,&q->mutex,&end);
        if(r==ETIMEDOUT)break;assert(!r);
    }
    bool ok=q->count<q->cap;
    if(ok){memcpy(q->data+((q->head+q->count)%q->cap)*q->item_size,item,q->item_size);q->count++;pthread_cond_broadcast(&q->cond);}
    pthread_mutex_unlock(&q->mutex);return ok?pdPASS:pdFALSE;
}
BaseType_t xQueueReceive(QueueHandle_t h,void *item,TickType_t timeout) {
    sim_queue*q=h;struct timespec end=time_after_ms(timeout);pthread_mutex_lock(&q->mutex);
    while(!q->count && timeout){
        int r=timeout==portMAX_DELAY?pthread_cond_wait(&q->cond,&q->mutex):pthread_cond_timedwait(&q->cond,&q->mutex,&end);
        if(r==ETIMEDOUT)break;assert(!r);
    }
    bool ok=q->count>0;
    if(ok){memcpy(item,q->data+q->head*q->item_size,q->item_size);q->head=(q->head+1)%q->cap;q->count--;pthread_cond_broadcast(&q->cond);}
    pthread_mutex_unlock(&q->mutex);return ok?pdPASS:pdFALSE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t h) {sim_queue*q=h;pthread_mutex_lock(&q->mutex);UBaseType_t n=q->count;pthread_mutex_unlock(&q->mutex);return n;}
void vQueueDelete(QueueHandle_t h) {sim_queue*q=h;pthread_cond_destroy(&q->cond);pthread_mutex_destroy(&q->mutex);free(q);}
static void *task_entry(void *p) {sim_task*t=p;current_task=t;t->fn(t->arg);return NULL;}
BaseType_t xTaskCreate(void(*fn)(void*),const char*name,uint32_t stack,void*arg,UBaseType_t priority,TaskHandle_t*out) {
    (void)name;(void)stack;(void)priority;
    sim_task*t=calloc(1,sizeof(*t));assert(t);t->fn=fn;t->arg=arg;
    pthread_mutex_init(&t->mutex,NULL);pthread_cond_init(&t->cond,NULL);assert(out);*out=t;
    assert(task_count<16);tasks[task_count++]=t;assert(!pthread_create(&t->thread,NULL,task_entry,t));return pdPASS;
}
static void cleanup_unlock(void *mutex) {pthread_mutex_unlock(mutex);}
void vTaskSuspend(TaskHandle_t h) {
    assert(!h);sim_task*t=current_task;
    pthread_cleanup_push(cleanup_unlock,&t->mutex);
    pthread_mutex_lock(&t->mutex);
    for(;;)pthread_cond_wait(&t->cond,&t->mutex);
    pthread_cleanup_pop(1);
}
void vTaskDelete(TaskHandle_t h) {
    if(h){sim_task*t=h;assert(!pthread_cancel(t->thread));return;}
    pthread_exit(NULL);
}
BaseType_t xTaskNotifyGive(TaskHandle_t h) {sim_task*t=h;assert(t);pthread_mutex_lock(&t->mutex);t->notifications++;pthread_cond_signal(&t->cond);pthread_mutex_unlock(&t->mutex);return pdPASS;}
void vTaskNotifyGiveFromISR(TaskHandle_t h,BaseType_t*w) {(void)xTaskNotifyGive(h);*w=pdTRUE;}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t timeout) {
    sim_task*t=current_task;assert(t);struct timespec end=time_after_ms(timeout);pthread_mutex_lock(&t->mutex);
    while(!t->notifications && timeout){int r=pthread_cond_timedwait(&t->cond,&t->mutex,&end);if(r==ETIMEDOUT)break;assert(!r);}
    unsigned n=t->notifications;if(n)t->notifications=clear?0:n-1;pthread_mutex_unlock(&t->mutex);return n;
}
static void join_tasks(void) {
    for(unsigned i=0;i<task_count;i++){sim_task*t=tasks[i];assert(!pthread_join(t->thread,NULL));pthread_mutex_destroy(&t->mutex);pthread_cond_destroy(&t->cond);free(t);}
    task_count=0;
}
void*heap_caps_malloc(size_t n,unsigned caps) {(void)caps;return malloc(n);}

/* GPIO model: a masked active-high level remains asserted until target RX is drained. */
static void(*gpio_handler)(void*);static void*gpio_arg;static bool irq_enabled;static int irq_type;
esp_err_t gpio_config(const gpio_config_t*c){(void)c;return ESP_OK;}
esp_err_t gpio_set_level(gpio_num_t p,uint32_t v){(void)p;(void)v;return ESP_OK;}
esp_err_t gpio_install_isr_service(int f){(void)f;return ESP_OK;}
esp_err_t gpio_isr_handler_add(gpio_num_t p,void(*fn)(void*),void*a){(void)p;gpio_handler=fn;gpio_arg=a;return ESP_OK;}
esp_err_t gpio_isr_handler_remove(gpio_num_t p){(void)p;gpio_handler=NULL;gpio_arg=NULL;return ESP_OK;}
esp_err_t gpio_intr_disable(gpio_num_t p){(void)p;irq_enabled=false;return ESP_OK;}
esp_err_t gpio_set_intr_type(gpio_num_t p,gpio_int_type_t t){(void)p;irq_type=t;return ESP_OK;}
esp_err_t gpio_intr_enable(gpio_num_t p){(void)p;irq_enabled=true;if(mock.target_to_host_available&&gpio_handler)gpio_handler(gpio_arg);return ESP_OK;}

static nrc_l2_stream_decoder_t target_decoder;
static uint8_t target_wire[NRC_L2_MAX_MESSAGE*2];static size_t target_wire_len;static unsigned target_slot_seq;
static atomic_bool inject_bus_error,corrupt_echo,block_credits;
static void target_response(uint8_t type,uint16_t seq,const void*p,uint16_t len) {
    size_t n;assert(!nrc_l2_message_encode(type,0,seq,p,len,target_wire+target_wire_len,sizeof(target_wire)-target_wire_len,&n));target_wire_len+=n;
}
static void target_command(const nrc_l2_message_view_t*m,void*context) {
    (void)context;
    uint8_t payload[128];size_t n;
    if(m->type==NRC_L2_MSG_HELLO_REQ){
        nrc_l2_hello_t h={.mac={2,1,2,3,4,5},.features=NRC_L2_FEATURE_ETHERNET_BRIDGE|NRC_L2_FEATURE_HOST_OWNS_IP|NRC_L2_FEATURE_PERIODIC_LINK_STATS|NRC_L2_FEATURE_INTERNAL_RATE_CONTROL|NRC_L2_FEATURE_HSPI_LOOPBACK,.max_ethernet_frame=1518,.minimum_stats_period_ms=50};
        n=nrc_l2_hello_encode(&h,payload,sizeof(payload));assert(n);target_response(NRC_L2_MSG_HELLO_RSP,m->sequence,payload,(uint16_t)n);
    }else if(m->type==NRC_L2_MSG_LOOPBACK_REQ){
        uint8_t echo[1518];assert(m->payload_length<=sizeof(echo));memcpy(echo,m->payload,m->payload_length);
        if(atomic_exchange(&corrupt_echo,false))echo[0]^=1;
        target_response(NRC_L2_MSG_LOOPBACK_RSP,m->sequence,echo,m->payload_length);
    }else if(m->type==NRC_L2_MSG_ETH_TX){
        target_response(NRC_L2_MSG_ETH_RX,m->sequence,m->payload,m->payload_length);
    }else{
        if(m->type==NRC_L2_MSG_CONNECT||m->type==NRC_L2_MSG_DISCONNECT){
            nrc_l2_wifi_event_t e={.event=m->type==NRC_L2_MSG_CONNECT?NRC_L2_WIFI_EVENT_CONNECTED:NRC_L2_WIFI_EVENT_DISCONNECTED};
            n=nrc_l2_wifi_event_encode(&e,payload,sizeof(payload));assert(n);target_response(NRC_L2_MSG_WIFI_EVENT,m->sequence,payload,(uint16_t)n);
        }
        nrc_l2_command_result_t r={.request_sequence=m->sequence,.result=NRC_L2_RESULT_OK};
        n=nrc_l2_command_result_encode(&r,payload,sizeof(payload));assert(n);target_response(NRC_L2_MSG_COMMAND_RESULT,m->sequence,payload,(uint16_t)n);
    }
}
esp_err_t spi_bus_initialize(spi_host_device_t h,const spi_bus_config_t*c,int dma){(void)h;(void)c;(void)dma;return ESP_OK;}
esp_err_t spi_bus_add_device(spi_host_device_t h,const spi_device_interface_config_t*c,spi_device_handle_t*out){(void)h;assert(c->mode==0);*out=&mock;return ESP_OK;}
esp_err_t spi_bus_remove_device(spi_device_handle_t d){(void)d;return ESP_OK;}
esp_err_t spi_bus_free(spi_host_device_t h){(void)h;return ESP_OK;}
esp_err_t spi_device_polling_transmit(spi_device_handle_t d,spi_transaction_t*t){
    assert(d==&mock);assert(t->length%8==0);
    if(atomic_exchange(&inject_bus_error,false))return ESP_FAIL;
    if(mock.phase==PHASE_COMMAND)mock.host_to_target_available=atomic_load(&block_credits)?0:4;
    int ret=mock_spi_transfer((char*)t->tx_buffer,(char*)t->rx_buffer,(int)(t->length/8));
    if(!ret&&mock.phase==PHASE_COMMAND&&mock.captured_length){
        (void)nrc_l2_stream_decoder_feed(&target_decoder,mock.captured,mock.captured_length,target_command,NULL);assert(!target_decoder.crc_errors && !target_decoder.framing_errors);mock.captured_length=0;
        if(target_wire_len){
            assert(!mock.target_to_host_available);mock.tx_slot_count=mock.tx_slot_index=0;
            prepare_rx_slots(target_wire,target_wire_len);
            for(unsigned i=0;i<mock.tx_slot_count;i++){
                uint8_t*s=mock.tx_slots[i];uint16_t c=(uint16_t)s[2]|((uint16_t)s[3]<<8);
                c=(c&0x3ffU)|((target_slot_seq++&63U)<<10);s[2]=(uint8_t)c;s[3]=(uint8_t)(c>>8);
            }
            target_wire_len=0;if(irq_enabled&&gpio_handler)gpio_handler(gpio_arg);
        }
    }
    return ret?ESP_FAIL:ESP_OK;
}
struct esp_netif_obj {esp_netif_driver_ifconfig_t driver;bool started,up;uint8_t mac[6];};
static atomic_uint ethernet_received;static atomic_bool reject_ethernet_rx;
esp_netif_t*esp_netif_new(const esp_netif_config_t*c){(void)c;return calloc(1,sizeof(esp_netif_t));}
void esp_netif_destroy(esp_netif_t*n){free(n);}
esp_err_t esp_netif_attach(esp_netif_t*n,esp_netif_iodriver_handle h){esp_netif_driver_base_t*b=h;return b->post_attach(n,h);}
esp_err_t esp_netif_set_driver_config(esp_netif_t*n,const esp_netif_driver_ifconfig_t*c){n->driver=*c;return ESP_OK;}
esp_err_t esp_netif_set_mac(esp_netif_t*n,const uint8_t*m){memcpy(n->mac,m,6);return ESP_OK;}
void esp_netif_action_start(void*h,void*b,int32_t e,void*d){(void)b;(void)e;(void)d;((esp_netif_t*)h)->started=true;}
void esp_netif_action_stop(void*h,void*b,int32_t e,void*d){(void)b;(void)e;(void)d;((esp_netif_t*)h)->started=false;}
void esp_netif_action_connected(void*h,void*b,int32_t e,void*d){(void)b;(void)e;(void)d;((esp_netif_t*)h)->up=true;}
void esp_netif_action_disconnected(void*h,void*b,int32_t e,void*d){(void)b;(void)e;(void)d;((esp_netif_t*)h)->up=false;}
esp_err_t esp_netif_receive(esp_netif_t*n,void*p,size_t len,void*eb){
    (void)eb;assert(len==1518);assert(n->driver.driver_free_rx_buffer);
    n->driver.driver_free_rx_buffer(n->driver.handle,p);atomic_fetch_add(&ethernet_received,1);
    return atomic_load(&reject_ethernet_rx)?ESP_FAIL:ESP_OK;
}
int main(void){
    mock_init();nrc_l2_stream_decoder_init(&target_decoder);
    nrc7292_handle_t d=NULL; nrc7292_config_t c=NRC7292_CONFIG_DEFAULT();
    c.pin_sclk=12;c.pin_mosi=11;c.pin_miso=13;c.pin_cs=10;c.pin_eirq=9;
    c.eirq_mode=NRC7292_EIRQ_ACTIVE_HIGH_LEVEL;
    nrc7292_config_t bad=c;bad.pin_mosi=bad.pin_miso;
    assert(nrc7292_init(&bad,&d)==ESP_ERR_INVALID_ARG);
    assert(nrc7292_init(&c,&d)==ESP_OK);
    assert(nrc7292_start(d,5000)==ESP_OK);assert(irq_type==GPIO_INTR_HIGH_LEVEL);
    uint8_t data[1518];
    for(unsigned i=0;i<128;i++){
        for(unsigned j=0;j<sizeof(data);j++)data[j]=(uint8_t)(i*73+j*17);
        size_t len=(i%2)?sizeof(data):i+1;
        assert(nrc7292_loopback(d,data,len,3000)==ESP_OK);
    }
    atomic_store(&corrupt_echo,true);assert(nrc7292_loopback(d,data,sizeof(data),3000)==ESP_FAIL);
    assert(nrc7292_loopback(d,data,sizeof(data),3000)==ESP_OK);
    atomic_store(&block_credits,true);
    vTaskDelay(220); /* let the idle I/O watchdog observe the new credit state */
    assert(nrc7292_loopback(d,data,sizeof(data),30)==ESP_ERR_TIMEOUT);
    atomic_store(&block_credits,false);vTaskDelay(20);
    assert(nrc7292_loopback(d,data,sizeof(data),3000)==ESP_OK);
    esp_netif_t*n=NULL;assert(nrc7292_create_netif(d,&n)==ESP_OK);assert(n&&n->mac[0]==2);
    assert(nrc7292_connect(d,3000)==ESP_OK);assert(nrc7292_is_connected(d));
    assert(n->driver.transmit(n->driver.handle,data,sizeof(data))==ESP_OK);
    for(unsigned i=0;i<1000&&!atomic_load(&ethernet_received);i++)vTaskDelay(1);
    assert(atomic_load(&ethernet_received)==1);
    /* IDF consumes a buffer even when input fails; ASan catches double-free. */
    atomic_store(&reject_ethernet_rx,true);
    assert(n->driver.transmit(n->driver.handle,data,sizeof(data))==ESP_OK);
    for(unsigned i=0;i<1000&&atomic_load(&ethernet_received)<2;i++)vTaskDelay(1);
    assert(atomic_load(&ethernet_received)==2);
    assert(nrc7292_disconnect(d,3000)==ESP_OK);assert(!nrc7292_is_connected(d));
    nrc7292_diagnostics_t diag;assert(nrc7292_get_diagnostics(d,&diag)==ESP_OK);
    assert(diag.irq_count>0);assert(diag.native.rx_slots>=320);assert(!diag.native.faulted);
    atomic_store(&inject_bus_error,true);
    for(unsigned i=0;i<1000;i++){nrc7292_get_diagnostics(d,&diag);if(diag.native.faulted)break;vTaskDelay(1);}
    assert(diag.native.faulted);
    assert(nrc7292_stop(d)==ESP_OK);join_tasks();
    /* A fresh session after hardware reboot starts with fresh native counters. */
    mock_init();target_slot_seq=0;nrc_l2_stream_decoder_init(&target_decoder);
    c.pin_eirq=GPIO_NUM_NC;c.eirq_mode=NRC7292_EIRQ_DISABLED;
    assert(nrc7292_init(&c,&d)==ESP_OK);assert(nrc7292_start(d,5000)==ESP_OK);
    assert(nrc7292_loopback(d,data,sizeof(data),3000)==ESP_OK);
    assert(nrc7292_stop(d)==ESP_OK);join_tasks();
    puts("production ESP adapter: 128 variable-size echoes, level IRQ, polling, timeout, L2 ownership, fault/stop/restart PASS (mock target)");return 0;
}
