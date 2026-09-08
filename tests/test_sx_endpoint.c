/* SPDX-License-Identifier: MIT
 * Production NRC application parser/echo and output backpressure tests.
 * SDK/RTOS/RF calls not reachable from these tests are garbage-collected.
 */
#include <assert.h>
#include "../sx_newah/L2BRIDGE_HSPI/l2bridge_main.c"

typedef struct {unsigned cap,size,head,count;unsigned char bytes[];} q_t;
QueueHandle_t xQueueCreate(UBaseType_t n,UBaseType_t size){q_t*q=calloc(1,sizeof(*q)+n*size);assert(q);q->cap=n;q->size=size;return q;}
BaseType_t xQueueSend(QueueHandle_t h,const void*p,TickType_t t){(void)t;q_t*q=h;if(q->count==q->cap)return 0;memcpy(q->bytes+((q->head+q->count)%q->cap)*q->size,p,q->size);q->count++;return pdPASS;}
BaseType_t xQueueReceive(QueueHandle_t h,void*p,TickType_t t){(void)t;q_t*q=h;if(!q->count)return 0;memcpy(p,q->bytes+q->head*q->size,q->size);q->head=(q->head+1)%q->cap;q->count--;return pdPASS;}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t h){return ((q_t*)h)->count;}
static uint32_t simulated_ms;
TickType_t xTaskGetTickCount(void){return simulated_ms;}
void vTaskDelay(TickType_t t){simulated_ms+=t;}
static uint32_t stall_writes;static bool inject_error;
static uint8_t output_bytes[NRC_L2_MAX_MESSAGE];static size_t output_size;
int _hif_write(char*p,int length){
    if(inject_error)return -1;
    if(stall_writes){stall_writes--;return 0;}
    int n=length<127?length:127;
    assert(n>0 && output_size+(size_t)n<=sizeof output_bytes);
    memcpy(output_bytes+output_size,p,(size_t)n);output_size+=(size_t)n;return n;
}
static uint8_t input[1518];static unsigned decoded_echo;
static void check_echo(const nrc_l2_message_view_t*m,void*ctx){
    (void)ctx;assert(m->type==NRC_L2_MSG_LOOPBACK_RSP && m->payload_length==1518);
    assert(!memcmp(m->payload,input,1518));decoded_echo++;
}
int main(void){
    q_rx_free=xQueueCreate(FRAME_POOL,sizeof(frame_t*));q_to_host=xQueueCreate(FRAME_POOL,sizeof(frame_t*));
    q_out=xQueueCreate(CONTROL_DEPTH,sizeof(control_t));q_control=xQueueCreate(CONTROL_DEPTH,sizeof(control_t));
    q_tx_free=xQueueCreate(FRAME_POOL,sizeof(frame_t*));q_to_radio=xQueueCreate(FRAME_POOL,sizeof(frame_t*));
    for(unsigned i=0;i<FRAME_POOL;i++){frame_t*p=&rx_pool[i];assert(xQueueSend(q_rx_free,&p,0)==pdPASS);}
    for(unsigned i=0;i<sizeof input;i++)input[i]=(uint8_t)(i*73+11);
    uint8_t record[NRC_L2_MAX_MESSAGE];size_t n;
    assert(!nrc_l2_message_encode(NRC_L2_MSG_LOOPBACK_REQ,0,77,input,sizeof input,record,sizeof record,&n));
    nrc_l2_stream_decoder_init(&decoder);
    /* No JP initialization/association is performed in this native echo path. */
    assert(!connected && !jp_ready);
    for(size_t at=0;at<n;){size_t count=n-at;if(count>37)count=37;hif_rx((char*)record+at,(int)count);at+=count;}
    frame_t*f=NULL;assert(xQueueReceive(q_to_host,&f,0)==pdPASS);
    assert(f->type==NRC_L2_MSG_LOOPBACK_RSP && f->sequence==77 && f->len==1518);
    assert(!memcmp(f->data,input,1518));
    stall_writes=10000; /* Simulate host absent for ten seconds: do not latch fatal fault. */
    assert(send_message(f->type,f->sequence,f->data,f->len));
    assert(simulated_ms==10000 && !transport_fault);
    nrc_l2_stream_decoder_t host_decoder;nrc_l2_stream_decoder_init(&host_decoder);
    (void)nrc_l2_stream_decoder_feed(&host_decoder,output_bytes,output_size,check_echo,NULL);
    assert(decoded_echo==1);assert(xQueueSend(q_rx_free,&f,0)==pdPASS);
    /* Full pool: bound memory and send BUSY, do not allocate a ninth frame. */
    for(unsigned i=0;i<FRAME_POOL;i++)hif_rx((char*)record,(int)n);
    assert(uxQueueMessagesWaiting(q_rx_free)==0 && uxQueueMessagesWaiting(q_to_host)==FRAME_POOL);
    hif_rx((char*)record,(int)n);control_t r;assert(xQueueReceive(q_out,&r,0)==pdPASS);
    nrc_l2_command_result_t result_value;
    assert(nrc_l2_command_result_decode(r.payload,r.length,&result_value));
    assert(result_value.request_sequence==77 && result_value.result==NRC_L2_RESULT_BUSY);
    /* An actual write error is a session fault; no automatic radio restart. */
    inject_error=true;output_size=0;assert(!send_message(NRC_L2_MSG_LOOPBACK_RSP,78,input,sizeof input));assert(transport_fault);
    free(q_rx_free);free(q_to_host);free(q_out);free(q_control);free(q_tx_free);free(q_to_radio);
    puts("production SX endpoint: fragmented echo, 10 s host absence, pool bounds, I/O failure PASS (SDK/RTOS stubs)");
    return 0;
}
