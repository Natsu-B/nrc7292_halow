#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nrc-hspi.h"

typedef enum {
    PHASE_COMMAND,
    PHASE_DATA,
    PHASE_CRC,
} mock_phase_t;

typedef struct {
    uint8_t registers[256];
    mock_phase_t phase;
    uint8_t address;
    uint16_t length;
    bool write;
    uint8_t tx_slots[4][HSPI_SLOT_SIZE_MAX];
    unsigned tx_slot_count;
    unsigned tx_slot_index;
    uint8_t captured[2048];
    size_t captured_length;
    uint8_t target_to_host_available;
    uint8_t host_to_target_available;
    unsigned status_command_count;
    unsigned fail_status_command_number;
    unsigned reject_commands;
    bool fail_queue_crc;
    uint8_t queue_error;
    unsigned last_host_sequence;
} mock_t;

static mock_t mock;

static void put_be16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void put_be32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static uint32_t get_be32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           src[3];
}

static void update_status_registers(void)
{
    uint8_t *status = &mock.registers[HSPI_REG_STATUS];
    memset(status, 0, sizeof(hspi_status_t));
    status[2] = 0; /* target->host queue error */
    status[3] = mock.target_to_host_available;
    put_be16(status + 4, HSPI_SLOT_SIZE_MAX / 4U);
    put_be16(status + 6,
             (uint16_t)(mock.target_to_host_available * (HSPI_SLOT_SIZE_MAX / 4U)));
    status[8] = mock.queue_error; /* host->target queue error */
    status[9] = mock.host_to_target_available;
    put_be16(status + 10, HSPI_SLOT_SIZE_MAX / 4U);
    put_be16(status + 12,
             (uint16_t)(mock.host_to_target_available * (HSPI_SLOT_SIZE_MAX / 4U)));
}

static int mock_spi_transfer(char *tx_buffer, char *rx_buffer, int length)
{
    assert(length > 0);
    if (rx_buffer != NULL) {
        memset(rx_buffer, 0, (size_t)length);
    }

    if (mock.phase == PHASE_COMMAND) {
        assert(length == 8);
        assert(tx_buffer != NULL && rx_buffer != NULL);
        const uint8_t *command = (const uint8_t *)tx_buffer;
        assert(command[4] == (uint8_t)((nrc_hspi_crc7_compute(command,4)<<1)|1U));
        if (mock.reject_commands) { --mock.reject_commands; return 0; }
        uint32_t opcode = get_be32(command);
        assert((opcode >> 24) == 0x50U);
        mock.address = (uint8_t)((opcode >> 13) & 0xFFU);
        mock.write = ((opcode >> 22) & 1U) != 0;
        bool burst = ((opcode >> 23) & 1U) != 0;
        mock.length = (uint16_t)(opcode & 0x1FFFU);
        ((uint8_t *)rx_buffer)[7] = HSPI_ACK_VALUE;

        if (!mock.write && burst && mock.address == HSPI_REG_STATUS) {
            ++mock.status_command_count;
            if (mock.fail_status_command_number != 0U &&
                mock.status_command_count == mock.fail_status_command_number) {
                return -1;
            }
        }

        if (!burst) {
            if (mock.write) {
                mock.registers[mock.address] = (uint8_t)opcode;
            } else {
                ((uint8_t *)rx_buffer)[6] = mock.registers[mock.address];
            }
            return 0;
        }
        mock.phase = PHASE_DATA;
        return 0;
    }

    if (mock.phase == PHASE_DATA) {
        assert(length == mock.length);
        if (mock.write) {
            assert(tx_buffer != NULL);
            if (mock.address == HSPI_REG_RXQ_WINDOW) {
                const uint8_t *slot = (const uint8_t *)tx_buffer;
                const uint16_t control = (uint16_t)slot[2] | ((uint16_t)slot[3] << 8);
                const uint16_t slot_len = control & 0x03FFU;
                assert(memcmp(slot, HSPI_SLOT_START, 2) == 0);
                mock.last_host_sequence = control >> 10;
                assert(slot_len <= HSPI_SLOT_SIZE_MAX - HSPI_SLOT_HDR_SIZE);
                assert(mock.captured_length + slot_len <= sizeof(mock.captured));
                memcpy(mock.captured + mock.captured_length,
                       slot + HSPI_SLOT_HDR_SIZE,
                       slot_len);
                mock.captured_length += slot_len;
                assert(mock.host_to_target_available > 0);
                --mock.host_to_target_available;
            } else {
                memcpy(&mock.registers[mock.address], tx_buffer, (size_t)length);
            }
        } else {
            assert(rx_buffer != NULL);
            if (mock.address == HSPI_REG_TXQ_WINDOW) {
                assert(mock.tx_slot_index < mock.tx_slot_count);
                memcpy(rx_buffer, mock.tx_slots[mock.tx_slot_index++], (size_t)length);
                assert(mock.target_to_host_available > 0);
                --mock.target_to_host_available;
            } else {
                if (mock.address == HSPI_REG_STATUS) {
                    update_status_registers();
                }
                memcpy(rx_buffer, &mock.registers[mock.address], (size_t)length);
            }
        }
        mock.phase = PHASE_CRC;
        return 0;
    }

    assert(mock.phase == PHASE_CRC);
    assert(length == 4);
    mock.phase = PHASE_COMMAND;
    if (mock.fail_queue_crc && mock.write && mock.address == HSPI_REG_RXQ_WINDOW) {
        mock.fail_queue_crc = false; return -1;
    }
    return 0;
}

static void mock_delay(uint32_t seconds)
{
    (void)seconds;
}

static void prepare_rx_slots(const uint8_t *data, size_t length)
{
    size_t offset = 0;
    unsigned sequence = 0;
    while (offset < length) {
        assert(mock.tx_slot_count < 4);
        uint8_t *slot = mock.tx_slots[mock.tx_slot_count++];
        memset(slot, 0, HSPI_SLOT_SIZE_MAX);
        memcpy(slot, HSPI_SLOT_START, 2);
        size_t chunk = length - offset;
        if (chunk > HSPI_SLOT_SIZE_MAX - HSPI_SLOT_HDR_SIZE) {
            chunk = HSPI_SLOT_SIZE_MAX - HSPI_SLOT_HDR_SIZE;
        }
        const uint16_t control = (uint16_t)(((uint16_t)sequence++ << 10) | chunk);
        slot[2] = (uint8_t)control;
        slot[3] = (uint8_t)(control >> 8);
        memcpy(slot + HSPI_SLOT_HDR_SIZE, data + offset, chunk);
        offset += chunk;
    }
    mock.target_to_host_available = (uint8_t)mock.tx_slot_count;
}

static void mock_init(void)
{
    memset(&mock, 0, sizeof(mock));
    mock.phase = PHASE_COMMAND;
    mock.host_to_target_available = 4;

    mock.registers[HSPI_REG_DEVICE_STATUS] = 1;
    mock.registers[HSPI_REG_CHIP_ID] = 0x72;
    mock.registers[HSPI_REG_CHIP_ID + 1] = 0x92;

    /* Message registers are read as big-endian 32-bit words, then compared as
     * host-memory ASCII by the reference implementation. */
    put_be32(&mock.registers[HSPI_REG_DEV_MSG_0], 0x2D43524EU); /* "NRC-" */
    put_be32(&mock.registers[HSPI_REG_DEV_MSG_1], 0x49505348U); /* "HSPI" */
    put_be32(&mock.registers[HSPI_REG_DEV_MSG_2],
             (4U << 16) | HSPI_SLOT_SIZE_MAX);
    put_be32(&mock.registers[HSPI_REG_DEV_MSG_3],
             (4U << 16) | HSPI_SLOT_SIZE_MAX);
    update_status_registers();
}

int main(void)
{
    hspi_ops_t operations = {
        .printf = NULL,
        .delay = mock_delay,
        .spi_transfer = mock_spi_transfer,
    };

    /* The message-register helper must not hide a preceding status-read error. */
    mock_init();
    mock.fail_status_command_number = 2;
    assert(nrc_hspi_open(&operations, HSPI_EIRQ_MODE_HIGH) == -1);

    mock_init();
    uint8_t target_data[700];
    uint8_t host_data[700];
    uint8_t read_back[1016];
    for (size_t i = 0; i < sizeof(target_data); ++i) {
        target_data[i] = (uint8_t)(i * 3U + 1U);
        host_data[i] = (uint8_t)(i * 7U + 9U);
    }
    prepare_rx_slots(target_data, sizeof(target_data));
    update_status_registers();

    assert(nrc_hspi_open(&operations, HSPI_EIRQ_MODE_HIGH) == 0);

    assert(nrc_hspi_write((char *)host_data, sizeof(host_data)) == (int)sizeof(host_data));
    assert(mock.captured_length == sizeof(host_data));
    assert(memcmp(mock.captured, host_data, sizeof(host_data)) == 0);

    memset(read_back, 0, sizeof(read_back));
    assert(nrc_hspi_read((char *)read_back, sizeof(read_back)) == (int)sizeof(target_data));
    assert(memcmp(read_back, target_data, sizeof(target_data)) == 0);

    nrc_hspi_queue_info_t info;
    assert(nrc_hspi_get_queue_info(&info) == 0);
    assert(info.host_to_target_slot_num == 4);
    assert(info.host_to_target_available == 2);
    assert(info.target_to_host_slot_num == 4);
    assert(info.target_to_host_available == 0);

    nrc_hspi_close();
    puts("hspi transport mock: ok");
    return 0;
}
