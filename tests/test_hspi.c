#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "nrc-hspi.h"

int main(void)
{
    _Static_assert(sizeof(hspi_cmd_t) == 8, "HSPI command must be 8 bytes");
    _Static_assert(sizeof(hspi_resp_t) == 8, "HSPI response must be 8 bytes");
    _Static_assert(sizeof(hspi_slot_t) == 4, "HSPI slot header must be 4 bytes");
    _Static_assert(sizeof(hspi_sys_t) == 16, "HSPI system register block must be 16 bytes");
    _Static_assert(sizeof(hspi_status_t) == 14, "HSPI status register block must be 14 bytes");

    const uint8_t read_sys_16[] = {0x50, 0x80, 0x00, 0x10};
    assert(nrc_hspi_crc7_compute(read_sys_16, sizeof(read_sys_16)) == 0x25U);
    assert(HSPI_OPCODE_READ_REG(HSPI_REG_SYS, 16) == 0x50800010U);
    assert(HSPI_OPCODE_WRITE_DATA(HSPI_REG_RXQ_WINDOW, 512) == 0x50E62200U);
    assert(HSPI_ACK_VALUE == 0x47U);

    assert(nrc_hspi_open(NULL, HSPI_EIRQ_MODE_NONE) == -1);
    hspi_ops_t invalid_operations = {0};
    assert(nrc_hspi_open(&invalid_operations, HSPI_EIRQ_MODE_NONE) == -1);

    puts("hspi layout/crc: ok");
    return 0;
}
