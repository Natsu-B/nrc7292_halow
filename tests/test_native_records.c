#define main original_hspi_smoke_test
#include "test_hspi_transport.c"
#undef main
#include "nrc_l2_protocol.h"
static uint8_t expected[1518];
static unsigned delivered;
static void received(const nrc_l2_message_view_t *m,void *arg) {
    unsigned i=*(unsigned*)arg;
    assert(m->type==NRC_L2_MSG_LOOPBACK_RSP);
    assert(m->sequence==(uint16_t)(i+1));
    assert(m->payload_length==sizeof(expected));
    assert(!memcmp(m->payload,expected,sizeof(expected)));delivered++;
}
int main(void) {
    mock_init();
    hspi_ops_t ops={.spi_transfer=mock_spi_transfer};
    assert(!nrc_hspi_open(&ops,HSPI_EIRQ_MODE_HIGH));
    unsigned rxseq=0;
    for(unsigned i=0;i<1000;i++) {
        uint8_t record[NRC_L2_MAX_MESSAGE],incoming[2032];size_t n;
        for(unsigned j=0;j<sizeof(expected);j++)expected[j]=(uint8_t)((j*73+i*37)^(i>>8));
        assert(!nrc_l2_message_encode(NRC_L2_MSG_LOOPBACK_RSP,0,(uint16_t)(i+1),expected,sizeof(expected),record,sizeof(record),&n));
        mock.captured_length=0;mock.host_to_target_available=4;
        assert(!nrc_hspi_refresh_status());
        /* Split a record at a non-slot and non-message boundary. */
        assert(nrc_hspi_write((char*)record,400)==400);
        mock.host_to_target_available=4;assert(!nrc_hspi_refresh_status());
        assert(nrc_hspi_write((char*)record+400,(int)n-400)==(int)n-400);
        assert(mock.captured_length==n && !memcmp(record,mock.captured,n));
        mock.tx_slot_index=mock.tx_slot_count=0;
        prepare_rx_slots(record,n);
        for(unsigned k=0;k<mock.tx_slot_count;k++) {
            uint8_t *p=mock.tx_slots[k];uint16_t v=((uint16_t)p[3]<<8)|p[2];
            v=(v&1023U)|(uint16_t)((rxseq++&63U)<<10);p[2]=(uint8_t)v;p[3]=(uint8_t)(v>>8);
        }
        assert(!nrc_hspi_refresh_status());
        assert(nrc_hspi_read((char*)incoming,sizeof(incoming))==(int)n);
        nrc_l2_stream_decoder_t decoder;nrc_l2_stream_decoder_init(&decoder);
        /* Actual native read byte stream then record parsing across callbacks. */
        assert(nrc_l2_stream_decoder_feed(&decoder,incoming,7,received,&i)==0);
        assert(nrc_l2_stream_decoder_feed(&decoder,incoming+7,n-7,received,&i)==1);
        assert(!decoder.crc_errors && !decoder.framing_errors);
    }
    nrc_hspi_diagnostics_t d;nrc_hspi_get_diagnostics(&d);
    assert(delivered==1000 && !d.faulted && !d.sequence_gaps && d.rx_slots==4000);
    nrc_hspi_close();puts("native slot/record integration: 1000 x 1518 bytes, sequence wrap and split callback PASS");return 0;
}
