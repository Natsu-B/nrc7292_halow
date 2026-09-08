/* Reuse the byte-accurate native register/slot mock; no RF is simulated. */
#define main original_hspi_smoke_test
#include "test_hspi_transport.c"
#undef main

static void open_native(void) {
    hspi_ops_t ops={.printf=NULL,.delay=mock_delay,.spi_transfer=mock_spi_transfer};
    assert(nrc_hspi_open(&ops,HSPI_EIRQ_MODE_HIGH)==0);
}
static void set_slot_sequence(unsigned index,unsigned seq) {
    uint8_t *p=mock.tx_slots[index];
    uint16_t word=(uint16_t)p[2]|((uint16_t)p[3]<<8);
    word=(word&1023U)|(uint16_t)((seq&63U)<<10);
    p[2]=(uint8_t)word;p[3]=(uint8_t)(word>>8);
}
int main(void) {
    uint8_t data[1518],buffer[2032];
    for(unsigned i=0;i<sizeof(data);i++)data[i]=(uint8_t)i;
    nrc_hspi_diagnostics_t diag;
    nrc_hspi_queue_info_t queues;

    /* Critical regression: zero credits replace a stale positive cache. */
    mock_init();open_native();mock.host_to_target_available=0;
    assert(nrc_hspi_refresh_status()==0);
    assert(nrc_hspi_get_queue_info(&queues)==0 && queues.host_to_target_available==0);
    assert(nrc_hspi_write((char*)data,sizeof(data))==0);
    assert(mock.captured_length==0);nrc_hspi_close();

    mock_init();open_native();mock.queue_error=1;
    assert(nrc_hspi_refresh_status()==-1);
    assert(nrc_hspi_write((char*)data,100)==-1);
    nrc_hspi_get_diagnostics(&diag);assert(diag.invalid_status==1 && diag.faulted);
    nrc_hspi_close();

    /* A bad slot header and a gap are fatal, never spliced into another frame. */
    mock_init();prepare_rx_slots(data,700);open_native();mock.tx_slots[0][0]='X';
    assert(nrc_hspi_read((char*)buffer,sizeof(buffer))==-1);
    nrc_hspi_get_diagnostics(&diag);assert(diag.bad_header==1 && diag.faulted);
    nrc_hspi_close();
    mock_init();prepare_rx_slots(data,700);set_slot_sequence(0,51);set_slot_sequence(1,53);open_native();
    assert(nrc_hspi_read((char*)buffer,sizeof(buffer))==-1);
    nrc_hspi_get_diagnostics(&diag);assert(diag.sequence_gaps==1 && diag.faulted);
    nrc_hspi_close();

    /* Reopen resets local TX sequence; first RX sequence may be nonzero. */
    mock_init();prepare_rx_slots(data,700);set_slot_sequence(0,63);set_slot_sequence(1,0);open_native();
    assert(nrc_hspi_read((char*)buffer,sizeof(buffer))==700);
    assert(!memcmp(buffer,data,700));
    assert(nrc_hspi_write((char*)data,10)==10 && mock.last_host_sequence==0);
    assert(nrc_hspi_write((char*)data,-1)==-1);
    assert(nrc_hspi_read((char*)buffer,-1)==-1);
    nrc_hspi_close();
    mock_init();open_native();assert(nrc_hspi_write((char*)data,10)==10 && mock.last_host_sequence==0);nrc_hspi_close();

    /* Payload already consumed but trailer transfer failed: no replay. */
    mock_init();open_native();mock.fail_queue_crc=true;
    assert(nrc_hspi_write((char*)data,700)==-1);
    size_t accepted=mock.captured_length;assert(accepted==508);
    assert(nrc_hspi_write((char*)data,700)==-1 && mock.captured_length==accepted);
    nrc_hspi_get_diagnostics(&diag);assert(diag.spi_errors==1 && diag.faulted);nrc_hspi_close();

    /* ACK retries are bounded to the reference driver's maximum. */
    mock_init();open_native();mock.reject_commands=100;
    assert(nrc_hspi_refresh_status()==-1);
    nrc_hspi_get_diagnostics(&diag);assert(diag.bad_ack==HSPI_XFER_RETRY_MAX && diag.faulted);nrc_hspi_close();

    puts("native HSPI fault injection: zero-credit, queue-error, malformed slot, sequence gap, reopen, negative lengths, uncertain write, bounded ACK retry PASS");
    return 0;
}
