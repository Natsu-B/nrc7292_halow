/*
 * MIT License
 *
 * Copyright (c) 2022 Newracom, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "nrc-hspi.h"


#define _hspi_log(...) do { \
    if (g_hspi_info.ops.printf) { \
        g_hspi_info.ops.printf(__VA_ARGS__); \
    } \
} while (0)
#define _hspi_read_debug(...) do { } while (0)
#define _hspi_write_debug(...) do { } while (0)

/**********************************************************************************************/

/* #define CONFIG_HSPI_BIG_ENDIAN */
/* #define CONFIG_HSPI_REG_PRINT */

static nrc_hspi_diagnostics_t diagnostics;
static uint8_t tx_sequence;
static uint8_t rx_expected;
static int rx_sequence_valid;

static hspi_info_t g_hspi_info =
{
	.active = 0,
};

#define HSPI_QUEUE_STATUS()					&g_hspi_info.queue.status

#define HSPI_TXQ_SLOT_NUM()					g_hspi_info.queue.slot[HSPI_TXQ].num
#define HSPI_TXQ_SLOT_SIZE()				g_hspi_info.queue.slot[HSPI_TXQ].size
#define HSPI_TXQ_SLOT_COUNT()				g_hspi_info.queue.status.txq.slot_cnt
#define HSPI_TXQ_SLOT_COUNT_UPDATE(c)		g_hspi_info.queue.status.txq.slot_cnt = (c)

#define HSPI_RXQ_SLOT_NUM()					g_hspi_info.queue.slot[HSPI_RXQ].num
#define HSPI_RXQ_SLOT_SIZE()				g_hspi_info.queue.slot[HSPI_RXQ].size
#define HSPI_RXQ_SLOT_COUNT()				g_hspi_info.queue.status.rxq.slot_cnt
#define HSPI_RXQ_SLOT_COUNT_UPDATE(c)		g_hspi_info.queue.status.rxq.slot_cnt = (c)

#define SPI_TRANSFER(tx, rx, len)			g_hspi_info.ops.spi_transfer(tx, rx, len)

static int HSPI_ACTIVE (void)
{
	if (!g_hspi_info.active)
		_hspi_log("hspi is not opened.\n");

	return g_hspi_info.active;
}

/**********************************************************************************************/

static uint8_t hspi_crc7 (const void *buffer, int len)
{
	const uint8_t *data = (const uint8_t *)buffer;
	uint8_t crc = 0;
	int i, j;

	for (i = 0 ; i < len ; i++)
	{
		crc ^= data[i];

		for (j = 0 ; j < 8 ; j++)
		{
			if (crc & 0x80)
				crc ^= 0x89;

			crc <<= 1;
		}
	}

	return crc >> 1;
}

static uint16_t hspi_convert_byteorder_16bit (uint16_t val)
{
	union
	{
		uint16_t s_val;
		uint8_t b_val[2];
	} src, dest;

#ifdef CONFIG_HOST_BIG_ENDIAN
	return val;
#endif

	src.s_val = val;

	dest.b_val[0] = src.b_val[1];
	dest.b_val[1] = src.b_val[0];

	return dest.s_val;
}

static uint32_t hspi_convert_byteorder_32bit (uint32_t val)
{
	union
	{
		uint32_t l_val;
		uint8_t b_val[4];
	} src, dest;

#ifdef CONFIG_HOST_BIG_ENDIAN
	return val;
#endif

	src.l_val = val;

	dest.b_val[0] = src.b_val[3];
	dest.b_val[1] = src.b_val[2];
	dest.b_val[2] = src.b_val[1];
	dest.b_val[3] = src.b_val[0];

	return dest.l_val;
}

#define _CPU_TO_BE16(val)	hspi_convert_byteorder_16bit(val)
#define _CPU_TO_BE32(val)	hspi_convert_byteorder_32bit(val)

#define _BE16_TO_CPU(val)	hspi_convert_byteorder_16bit(val)
#define _BE32_TO_CPU(val)	hspi_convert_byteorder_32bit(val)

/**********************************************************************************************/

static void hspi_opcode_print (hspi_opcode_t *opcode)
{
	_hspi_log("[ HSPI Operation Code ]\n");
	_hspi_log(" - value  : 0x%08X\n", opcode->val);
	_hspi_log(" - start  : 0x%02X\n", opcode->start);
	_hspi_log(" - burst  : %u\n", opcode->burst);
	_hspi_log(" - write  : %u\n", opcode->write);
	_hspi_log(" - fixed  : %u\n", opcode->fixed);
	_hspi_log(" - address: 0x%02X\n", opcode->address);

	if (opcode->write && !opcode->burst && opcode->fixed)
	{
		_hspi_log(" - data	  : 0x%02X\n", opcode->length & 0xff);
	}
	else
	{
		_hspi_log(" - length	: %u\n", opcode->length);
	}
}

static void hspi_transfer_setup (hspi_xfer_t *xfer, void *tx_buf, void *rx_buf, int len)
{
	memset(xfer, 0, sizeof(hspi_xfer_t));

	xfer->len = len;
	xfer->tx_buf = (char *)tx_buf;
	xfer->rx_buf = (char *)rx_buf;
}

static int hspi_transfer (hspi_opcode_t *opcode, char *buf, int len)
{
	const int retry_max = HSPI_XFER_RETRY_MAX;
	hspi_xfer_t xfers[HSPI_XFER_NUM_MAX];
	hspi_xfer_t *p_xfers = xfers;
	hspi_cmd_t cmd;
	hspi_resp_t resp;
	uint32_t burst_crc = 0;
	int retry;
	int ret = -1;
	int i;

	if (!HSPI_ACTIVE() || !opcode || diagnostics.faulted)
		return -1;

	if (opcode->burst)
	{
		size_t chunk_count;

		if (!buf || len <= 0 || len > 0x1fff)
			return -1;

		chunk_count = ((size_t)len + HSPI_XFER_LEN_MAX - 1U) / HSPI_XFER_LEN_MAX;
		if (chunk_count + 2U > HSPI_XFER_NUM_MAX)
			return -1;
	}
	else if (!opcode->write && (!buf || len <= 0))
	{
		return -1;
	}

	memset(&cmd, 0, sizeof(cmd));
	memset(&resp, 0, sizeof(resp));

/*	hspi_opcode_print(opcode); */

	cmd.opcode.val = _CPU_TO_BE32(opcode->val);
	cmd.crc = (hspi_crc7((char *)&cmd.opcode, sizeof(hspi_opcode_t)) << 1) | 0x01;

	hspi_transfer_setup(p_xfers++, &cmd, &resp, 8);

	if (opcode->burst)
	{
		int max_xfer_len = HSPI_XFER_LEN_MAX;

		if (opcode->write)
		{
			for (i = 0 ; i < len ; i += max_xfer_len)
			{
				if ((len - i) >= max_xfer_len)
					hspi_transfer_setup(p_xfers++, buf + i, NULL, max_xfer_len);
				else
					hspi_transfer_setup(p_xfers++, buf + i, NULL, len - i);
			}

			burst_crc = ~0;
			hspi_transfer_setup(p_xfers++, &burst_crc, NULL, 4);
		}
		else
		{
			for (i = 0 ; i < len ; i += max_xfer_len)
			{
				if ((len - i) >= max_xfer_len)
					hspi_transfer_setup(p_xfers++, NULL, buf + i, max_xfer_len);
				else
					hspi_transfer_setup(p_xfers++, NULL, buf + i, len - i);
			}

			burst_crc = 0;
			hspi_transfer_setup(p_xfers++, NULL, &burst_crc, 4);
		}
	}

	for (retry = 0 ; retry < retry_max ; retry++)
	{
		diagnostics.transactions++;
		ret = SPI_TRANSFER(xfers[0].tx_buf, xfers[0].rx_buf, xfers[0].len);
		if (ret != 0) { diagnostics.spi_errors++; break; }

		if (resp.ack != HSPI_ACK_VALUE) diagnostics.bad_ack++;
		if (resp.ack == HSPI_ACK_VALUE)
		{
/*			if (retry > 0)
				_hspi_log("hspi_transfer: retry=%d/%d\n", retry, retry_max); */

			if (opcode->burst)
			{
				int n_xfers = p_xfers - xfers;

				for (i = 1 ; i < n_xfers ; i++)
				{
					ret = SPI_TRANSFER(xfers[i].tx_buf, xfers[i].rx_buf, xfers[i].len);
					if (ret != 0) { diagnostics.spi_errors++; break; }
				}
			}
			else if (opcode->fixed && !opcode->write) /* single read */
				*buf = resp.data;

			break;
		}
	}

	if (ret != 0 || retry >= retry_max)
	{
        /* A queue burst may have been consumed. Never silently replay it. */
        diagnostics.faulted = 1;
/*		_hspi_log("hspi_transfer: retry=%d/%d ret=%d\n", retry, retry_max, ret); */
/*		hspi_opcode_print(opcode); */
		ret = -1;
	}

	return ret;
}

/**********************************************************************************************/

static int hspi_reg_read (uint8_t addr, char *data, int len)
{
	if (!data || !len)
		return -1;

	if (HSPI_ACTIVE())
	{
		hspi_opcode_t opcode;

		opcode.val = HSPI_OPCODE_READ_REG(addr, len);

		return hspi_transfer(&opcode, data, len);
	}

	return -1;
}

static int hspi_reg_write (uint8_t addr, uint8_t data)
{
	if (HSPI_ACTIVE())
	{
		hspi_opcode_t opcode;

		opcode.val = HSPI_OPCODE_WRITE_REG(addr, data);

		return hspi_transfer(&opcode, NULL, -1);
	}

	return -1;
}

/**********************************************************************************************/

static int hspi_regs_read_sys (hspi_sys_t *sys)
{
	hspi_sys_t sys_tmp;
	int err;

	err = hspi_reg_read(HSPI_REG_SYS, (char *)&sys_tmp, sizeof(hspi_sys_t));
	if (!err)
	{
		sys->status.ready = sys_tmp.status.ready;
		sys->status.sleep = sys_tmp.status.sleep;

		sys->chip_id = _BE16_TO_CPU(sys_tmp.chip_id);
		sys->modem_id = _BE32_TO_CPU(sys_tmp.modem_id);
		sys->sw_id = _BE32_TO_CPU(sys_tmp.sw_id);
		sys->board_id = _BE32_TO_CPU(sys_tmp.board_id);
	}

	return err;
}

static int hspi_regs_read_eirq (hspi_eirq_t *eirq)
{
	return hspi_reg_read(HSPI_REG_EIRQ, (char *)eirq, sizeof(hspi_eirq_t));
}

static int hspi_regs_read_status (hspi_status_t *status)
{
	hspi_status_t status_tmp;
	int err;

	err = hspi_reg_read(HSPI_REG_STATUS, (char *)&status_tmp, sizeof(hspi_status_t));
	if (!err)
	{
		status->eirq.txq = status_tmp.eirq.txq;
		status->eirq.rxq = status_tmp.eirq.rxq;
		status->eirq.ready = status_tmp.eirq.ready;
		status->eirq.sleep = status_tmp.eirq.sleep;

		status->txq.error = status_tmp.txq.error;
		status->txq.slot_cnt = status_tmp.txq.slot_cnt;
		status->txq.slot_size = _BE16_TO_CPU(status_tmp.txq.slot_size);
		status->txq.total_slot_size = _BE16_TO_CPU(status_tmp.txq.total_slot_size);

		status->rxq.error = status_tmp.rxq.error;
		status->rxq.slot_cnt = status_tmp.rxq.slot_cnt;
		status->rxq.slot_size = _BE16_TO_CPU(status_tmp.rxq.slot_size);
		status->rxq.total_slot_size = _BE16_TO_CPU(status_tmp.rxq.total_slot_size);
	}

	return err;
}

static int hspi_regs_read_message (hspi_msg_t msg)
{
	hspi_status_t status_tmp;
	hspi_msg_t msg_tmp;
	int err;

	if (!msg)
		return -1;

	memset(&status_tmp, 0, sizeof(status_tmp));
	memset(&msg_tmp, 0, sizeof(msg_tmp));
	err = hspi_reg_read(HSPI_REG_STATUS, (char *)&status_tmp, sizeof(hspi_status_t));
	if (err)
		return err;

	err = hspi_reg_read(HSPI_REG_MSG, (char *)msg_tmp, sizeof(hspi_msg_t));
	if (!err)
	{
		msg[0] = _BE32_TO_CPU(msg_tmp[0]);
		msg[1] = _BE32_TO_CPU(msg_tmp[1]);
		msg[2] = _BE32_TO_CPU(msg_tmp[2]);
		msg[3] = _BE32_TO_CPU(msg_tmp[3]);
	}

	return err;
}

static int hspi_regs_read_all (hspi_regs_t *regs)
{
	if (!regs)
		return -1;

	if (hspi_regs_read_sys(&regs->sys) != 0)
		return -1;
	if (hspi_regs_read_eirq(&regs->eirq) != 0)
		return -1;
	if (hspi_regs_read_status(&regs->status) != 0)
		return -1;
	return hspi_regs_read_message(regs->msg);
}

/**********************************************************************************************/

#ifdef CONFIG_HSPI_REG_PRINT
static void hspi_regs_print_sys (hspi_sys_t *sys)
{
	_hspi_log("\r\n");
	_hspi_log("[ HSPI SYS Registers ]\n");
	_hspi_log(" - Status	  : Ready(%u), Sleep(%u)\n", sys->status.ready, sys->status.sleep);
	_hspi_log(" - Chip ID	 : %04X\n", sys->chip_id);
	_hspi_log(" - Modem ID	: %08X\n", sys->modem_id);
	_hspi_log(" - Software ID : %08X\n", sys->sw_id);
	_hspi_log(" - Board ID	: %08X\n", sys->board_id);
}

static void hspi_regs_print_eirq (hspi_eirq_t *eirq)
{
	_hspi_log("\r\n");
	_hspi_log("[ HSPI EIRQ Registers ]\n");
	_hspi_log(" - mode   : active(%s), trigger(%s), %s\n",
					eirq->mode.active ? "high" : "low",
					eirq->mode.trigger ? "edge" : "level",
					eirq->mode.io_enable ? "enable" : "disable");
	_hspi_log(" - enable : txq(%u), rxq(%u), ready(%u), sleep(%u)\n",
					eirq->enable.txq, eirq->enable.rxq,
					eirq->enable.ready, eirq->enable.sleep);
}

static void hspi_regs_print_status (hspi_status_t *status)
{
	_hspi_log("\r\n");
	_hspi_log("[ HSPI STATUS Registers ]\n");
	_hspi_log(" - latch : 0x%02X\n", status->latch);
	_hspi_log(" - eirq  : txque(%u), rxque(%u), ready(%u), sleep(%u)\n",
				status->eirq.txq, status->eirq.rxq,
				status->eirq.ready, status->eirq.sleep);
	_hspi_log(" - txq   : error(0x%02X), slot_cnt(%u), slot_size(%u/%u)\n",
				status->txq.error, status->txq.slot_cnt,
				status->txq.slot_size, status->txq.total_slot_size);
	_hspi_log(" - rxq   : error(0x%02X), slot_cnt(%u), slot_size(%u/%u)\n",
				status->rxq.error, 	status->rxq.slot_cnt,
				status->rxq.slot_size, status->rxq.total_slot_size);
}

static void hspi_regs_print_message (hspi_msg_t msg)
{
	_hspi_log("\r\n");
	_hspi_log("[ HSPI MESSAGE Registers ]\n");
	_hspi_log(" - message 0 : 0x%08X\n", msg[0]);
	_hspi_log(" - message 1 : 0x%08X\n", msg[1]);
	_hspi_log(" - message 2 : 0x%08X\n", msg[2]);
	_hspi_log(" - message 3 : 0x%08X\n", msg[3]);
}

static void hspi_regs_print_all (hspi_regs_t *regs)
{
	hspi_regs_print_sys(&regs->sys);
	hspi_regs_print_eirq(&regs->eirq);
	hspi_regs_print_status(&regs->status);
	hspi_regs_print_message(regs->msg);

	_hspi_log("\n");
}
#else
#define hspi_regs_print_sys(sys)
#define hspi_regs_print_eirq(eirq)
#define hspi_regs_print_status(status)
#define hspi_regs_print_message(msg)
#define hspi_regs_print_all(regs)
#endif /* #ifdef CONFIG_HSPI_REG_PRINT */

/**********************************************************************************************/

static int hspi_eirq_enable (int mode, int enable)
{
	hspi_eirq_t eirq;
	memset(&eirq, 0, sizeof(eirq));

/*	_hspi_log("mode(0x%X), enable(0x%X)\n", mode & 0x3, enable & 0xf); */

	eirq.mode.active = !!(mode & HSPI_EIRQ_HIGH);
	eirq.mode.trigger = !!(mode & HSPI_EIRQ_EDGE);
	eirq.mode.io_enable = 1;

	eirq.enable.txq = !!(enable & HSPI_EIRQ_TXQ);
	eirq.enable.rxq = !!(enable & HSPI_EIRQ_RXQ);
	eirq.enable.ready = !!(enable & HSPI_EIRQ_READY);
	eirq.enable.sleep = !!(enable & HSPI_EIRQ_SLEEP);

	if (hspi_reg_write(HSPI_REG_EIRQ_MODE, eirq.val[0]) != 0)
		return -1;

	if (hspi_reg_write(HSPI_REG_EIRQ_ENABLE, eirq.val[1]) != 0)
		return -1;

#ifdef CONFIG_HSPI_REG_PRINT
	memset(&eirq, 0, sizeof(eirq));

	if (hspi_regs_read_eirq(&eirq) == 0)
		hspi_regs_print_eirq(&eirq);
#endif

	return 0;
}

static int hspi_eirq_disable (void)
{
	hspi_eirq_t eirq;
	memset(&eirq, 0, sizeof(eirq));

	eirq.mode.active = 0;
	eirq.mode.trigger = 0;

	eirq.enable.txq = 0;
	eirq.enable.rxq = 0;
	eirq.enable.ready = 0;
	eirq.enable.sleep = 0;

	if (hspi_reg_write(HSPI_REG_EIRQ_MODE, eirq.val[0]) != 0)
		return -1;

	if (hspi_reg_write(HSPI_REG_EIRQ_ENABLE, eirq.val[1]) != 0)
		return -1;

	return 0;
}

/**********************************************************************************************/

static void hspi_status_init (int que)
{
	hspi_status_t *status = HSPI_QUEUE_STATUS();

	if (que == HSPI_TXQ || que == HSPI_QUE_ALL)
		memset(&status->txq, 0, sizeof(status->txq));

	if (que == HSPI_RXQ || que == HSPI_QUE_ALL)
		memset(&status->rxq, 0, sizeof(status->rxq));
}

static int hspi_status_update (void)
{
    hspi_status_t fresh;
    hspi_status_t *cached = HSPI_QUEUE_STATUS();
    if (hspi_regs_read_status(&fresh) != 0) return -1;
    /* The hardware reports queue lengths in words, including the slot header.
     * Zero is a valid credit count. Keeping old credits on zero overflows RXQ. */
    const unsigned counts[2] = {fresh.txq.slot_cnt, fresh.rxq.slot_cnt};
    const unsigned sizes[2] = {fresh.txq.slot_size, fresh.rxq.slot_size};
    const unsigned totals[2] = {fresh.txq.total_slot_size, fresh.rxq.total_slot_size};
    const unsigned errors[2] = {fresh.txq.error, fresh.rxq.error};
    for (unsigned q = 0; q < 2; ++q) {
        const unsigned max_count = g_hspi_info.queue.slot[q].num < HSPI_SLOT_CNT_MAX ?
            g_hspi_info.queue.slot[q].num : HSPI_SLOT_CNT_MAX;
        if (errors[q] || counts[q] > max_count ||
            (counts[q] && (sizes[q] * 4U != g_hspi_info.queue.slot[q].size ||
                          totals[q] != counts[q] * sizes[q])) ||
            (!counts[q] && totals[q])) {
            memset(cached, 0, sizeof(*cached));
            diagnostics.invalid_status++;
            diagnostics.faulted = 1;
            return -1;
        }
    }
    *cached = fresh;
    return 0;
}

/**********************************************************************************************/

static int hspi_read_slot (int slot_num, int slot_size, char *buf, int *len)
{
	uint8_t slot_buf[HSPI_SLOT_SIZE_MAX];
	hspi_opcode_t opcode;
	int i;
	int j = 0;

	if (!buf || !len || *len <= 0 || slot_size <= HSPI_SLOT_HDR_SIZE ||
			slot_size > HSPI_SLOT_SIZE_MAX)
		return -1;

	for (i = 0 ; i < slot_num ; i++)
	{
		opcode.val = HSPI_OPCODE_READ_DATA(HSPI_REG_TXQ_WINDOW, slot_size);

		if (hspi_transfer(&opcode, (char *)slot_buf, slot_size) != 0)
			return -1;

		/* The on-wire slot control word is little-endian: length occupies
		 * bits 0..9 and sequence occupies bits 10..15. Decode bytes directly
		 * instead of casting a byte buffer to an aligned C bit-field struct. */
		uint16_t control = (uint16_t)slot_buf[2] |
					   ((uint16_t)slot_buf[3] << 8);
		uint16_t slot_len = control & 0x03FFU;
		uint8_t slot_seq = (uint8_t)((control >> 10) & HSPI_SLOT_SEQ_MAX);

		if (memcmp(slot_buf, HSPI_SLOT_START, HSPI_SLOT_START_SIZE) != 0 ||
				slot_len > (uint16_t)(slot_size - HSPI_SLOT_HDR_SIZE) ||
				j > *len - (int)slot_len)
		{
			_hspi_log("hspi_read: invalid header, start=%c(%X),%c(%X) len=%u \n",
						slot_buf[0], slot_buf[0], slot_buf[1], slot_buf[1],
						slot_len);
            diagnostics.bad_header++;
            diagnostics.faulted = 1;
			return -1;
		}

		memcpy(buf + j, slot_buf + HSPI_SLOT_HDR_SIZE, slot_len);
		j += slot_len;

		_hspi_read_debug("slot: seq=%u len=%u\n", slot_seq, slot_len);

		/* Accept the first sequence after host reopen. Thereafter a gap is
         * a stream fault, not an invitation to splice two Ethernet frames. */
        if (rx_sequence_valid && slot_seq != rx_expected) {
            diagnostics.sequence_gaps++;
            diagnostics.faulted = 1;
            return -1;
        }
        rx_sequence_valid = 1;
        rx_expected = (uint8_t)((slot_seq + 1U) & HSPI_SLOT_SEQ_MAX);
        diagnostics.rx_slots++;
	}

	*len = j;

	return i;
}

static int hspi_read (char *buf, int len)
{
	uint16_t slot_size;
	uint8_t slot_cnt;
	uint16_t slot_num;
	int ret;

	if (!buf || len <= 0)
		return -1;

	if (!HSPI_ACTIVE() || diagnostics.faulted)
		return -1;

	slot_cnt = HSPI_TXQ_SLOT_COUNT();
	slot_size = HSPI_TXQ_SLOT_SIZE();
	slot_num = len / (slot_size - HSPI_SLOT_HDR_SIZE);

	if (slot_num > HSPI_TXQ_SLOT_NUM())
		slot_num = HSPI_TXQ_SLOT_NUM();

	if (slot_num > slot_cnt)
	{
		if (hspi_status_update() != 0)
		{
			_hspi_read_debug("status update fail\n");
			return -1;
		}

		slot_cnt = HSPI_TXQ_SLOT_COUNT();

		if (slot_num > slot_cnt)
			slot_num = slot_cnt;
	}

	if (slot_num == 0)
		return 0;

	_hspi_read_debug("slot_cnt=%u slot_num=%u len=%d\n", slot_cnt, slot_num, len);

	ret = hspi_read_slot(slot_num, slot_size, buf, &len);
	if (ret < 0)
		hspi_status_init(HSPI_TXQ);
	else
	{
		slot_num = ret;
		ret = len;

		if (slot_num > 0)
		{
			slot_cnt -= slot_num;
			HSPI_TXQ_SLOT_COUNT_UPDATE(slot_cnt);
		}
	}

	_hspi_read_debug("slot_cnt=%u slot_num=%u ret=%d\n", slot_cnt, slot_num, ret);

	return ret;
}

static int hspi_write_slot (int slot_num, int slot_size, char *buf, int *len)
{
	uint8_t slot_buf[HSPI_SLOT_SIZE_MAX];
	hspi_opcode_t opcode;
	int i;
	int j = 0;

	if (!buf || !len || *len <= 0 || slot_size <= HSPI_SLOT_HDR_SIZE ||
			slot_size > HSPI_SLOT_SIZE_MAX)
		return -1;

	for (i = 0 ; i < slot_num ; i++)
	{
		int slot_len = *len - j;
		if (slot_len > slot_size - HSPI_SLOT_HDR_SIZE)
			slot_len = slot_size - HSPI_SLOT_HDR_SIZE;
		if (slot_len <= 0)
			break;

		memcpy(slot_buf, HSPI_SLOT_START, HSPI_SLOT_START_SIZE);
		uint16_t control = (uint16_t)(((uint16_t)tx_sequence << 10) |
								 (uint16_t)slot_len);
		slot_buf[2] = (uint8_t)control;
		slot_buf[3] = (uint8_t)(control >> 8);
		memcpy(slot_buf + HSPI_SLOT_HDR_SIZE, buf + j, (size_t)slot_len);
		memset(slot_buf + HSPI_SLOT_HDR_SIZE + slot_len, 0,
				(size_t)(slot_size - HSPI_SLOT_HDR_SIZE - slot_len));

		_hspi_write_debug("slot: seq=%u len=%u\n", tx_sequence, slot_len);

		opcode.val = HSPI_OPCODE_WRITE_DATA(HSPI_REG_RXQ_WINDOW, slot_size);

		if (hspi_transfer(&opcode, (char *)slot_buf, slot_size) != 0)
			break;

		j += slot_len;

		tx_sequence = (uint8_t)((tx_sequence + 1U) & HSPI_SLOT_SEQ_MAX);
        diagnostics.tx_slots++;
	}

	*len = j;

	return i;
}

static int hspi_write (char *buf, int len)
{
	uint8_t slot_cnt;
	uint16_t slot_size;
	uint16_t slot_num;
	int ret;

	if (!buf || len <= 0)
		return -1;

	if (!HSPI_ACTIVE() || diagnostics.faulted)
		return -1;

	slot_cnt = HSPI_RXQ_SLOT_COUNT();
	slot_size = HSPI_RXQ_SLOT_SIZE();
	slot_num = (len + (slot_size - HSPI_SLOT_HDR_SIZE - 1)) / (slot_size - HSPI_SLOT_HDR_SIZE);

	if (slot_num > slot_cnt)
	{
		if (hspi_status_update() != 0)
		{
			_hspi_write_debug("status update fail\n");
			return -1;
		}

		slot_cnt = HSPI_RXQ_SLOT_COUNT();
	}

	if (slot_num <= slot_cnt)
	{
		_hspi_write_debug("slot_cnt=%u slot_num=%u len=%d\n", slot_cnt, slot_num, len);
	}
	else
	{
		int _len = slot_cnt * (slot_size - HSPI_SLOT_HDR_SIZE);

		_hspi_write_debug("slot_cnt=%u slot_num=%u->%u len=%d->%d\n",
							slot_cnt, slot_num, slot_cnt, len, _len);

		slot_num = slot_cnt;
		len = _len;
	}

    if (slot_num == 0) return 0; /* native nonblocking backpressure */
	ret = hspi_write_slot(slot_num, slot_size, buf, &len);
    if (diagnostics.faulted) {
        hspi_status_init(HSPI_RXQ);
        return -1;
    }
	if (ret < slot_num)
	{
		hspi_status_init(HSPI_RXQ);
		_hspi_write_debug("slot_cnt=%u slot_num=%u ret=%d\n", 0U, (unsigned)ret, len);
	}
	else
	{
		slot_cnt -= slot_num;
		HSPI_RXQ_SLOT_COUNT_UPDATE(slot_cnt);
		_hspi_write_debug("slot_cnt=%u slot_num=%u ret=%d\n", slot_cnt, slot_num, len);
	}

	return len;
}

/**********************************************************************************************/

static int hspi_reset (void)
{
	return hspi_reg_write(HSPI_REG_DEVICE_STATUS, 0xC8);
}

static int hspi_ready (hspi_info_t *info)
{
	const int timeout = 1; /* host owner supplies the bounded retry policy */
	hspi_regs_t regs;
	int i;

	if (!HSPI_ACTIVE() || !info)
		return -1;

	memset(&regs, 0, sizeof(regs));
	if (hspi_regs_read_all(&regs) != 0)
		return -1;

    diagnostics.chip_id = regs.sys.chip_id;
    diagnostics.modem_id = regs.sys.modem_id;
    diagnostics.sw_id = regs.sys.sw_id;
    diagnostics.board_id = regs.sys.board_id;
    memcpy(diagnostics.signature, regs.msg, 8);
    diagnostics.signature[8] = '\0';
    if (regs.sys.chip_id != 0x7292U) return -1;
	hspi_regs_print_all(&regs);

	for (i = 0 ; i < timeout ; i++)
	{
		if (memcmp(regs.msg, "NRC-HSPI", 8) == 0)
		{
			uint16_t slot_num;
			uint16_t slot_size;
			int que;

			for (que = HSPI_TXQ ; que <= HSPI_RXQ ; que++)
			{
				slot_num = (regs.msg[2 + que] >> 16) & 0xffff;
				slot_size = regs.msg[2 + que] & 0xffff;

				if (slot_num == 0)
					return -1;

				if (slot_size <= HSPI_SLOT_HDR_SIZE || slot_size > HSPI_SLOT_SIZE_MAX || (slot_size & 3U))
					return -1;

				info->queue.slot[que].num = slot_num;
				info->queue.slot[que].size = slot_size;
			}
#if 0
			_hspi_log("[ HSPI_SLOT ]\n");
			_hspi_log(" - hdr: %d-byte\n", HSPI_SLOT_HDR_SIZE);
			_hspi_log(" - txq: (%u x %u)-byte\n",
					info->queue.slot[HSPI_TXQ].num,
					info->queue.slot[HSPI_TXQ].size);
			_hspi_log(" - rxq: (%u x %u)-byte\n",
					info->queue.slot[HSPI_RXQ].num,
					info->queue.slot[HSPI_RXQ].size);
			_hspi_log("\r\n");
#endif
			return 0;
		}


	}

	_hspi_log("No compatible NRC-HSPI firmware\n");

	return -1;
}

int nrc_hspi_set_eirq_mode(enum HSPI_EIRQ_MODE mode)
{
    if (!HSPI_ACTIVE()) return -1;
    switch (mode) {
    case HSPI_EIRQ_MODE_LOW:
    case HSPI_EIRQ_MODE_HIGH:
    case HSPI_EIRQ_MODE_RISING:
    case HSPI_EIRQ_MODE_FALLING:
        return hspi_eirq_enable(mode, HSPI_EIRQ_ALL);
    default:
        return hspi_eirq_disable();
    }
}

static int hspi_open (hspi_ops_t *ops, enum HSPI_EIRQ_MODE mode)
{

    memset(&diagnostics, 0, sizeof(diagnostics));
    tx_sequence = 0; rx_expected = 0; rx_sequence_valid = 0;
	memset(&g_hspi_info, 0, sizeof(hspi_info_t));
	memcpy(&g_hspi_info.ops, ops, sizeof(hspi_ops_t));

	g_hspi_info.active = 1;

	if (hspi_ready(&g_hspi_info) != 0 || hspi_status_update() != 0)
	{
		memset(&g_hspi_info, 0, sizeof(hspi_info_t));
		return -1;
	}

    return nrc_hspi_set_eirq_mode(mode);
}

static void hspi_close (void)
{
	memset(&g_hspi_info, 0, sizeof(hspi_info_t));
}

/**********************************************************************************************/

int nrc_hspi_reset (void)
{
	return hspi_reset();
}

int nrc_hspi_open (hspi_ops_t *ops, enum HSPI_EIRQ_MODE mode)
{
	if (!ops)
	{
		_hspi_log("hspi_open: ops=NULL\n");
		return -1;
	}
	if (!ops->spi_transfer)
	{
		_hspi_log("hspi_open: spi_transfer=NULL\n");
		return -1;
	}

/*	printf("delay=%p printf==%p spi_transfer=%p\n", ops->delay, ops->printf, ops->spi_transfer); */

	return hspi_open(ops, mode);
}

void nrc_hspi_close (void)
{
	hspi_close();
}

int nrc_hspi_read (char *buf, int len)
{
	return hspi_read(buf, len);
}

int nrc_hspi_write (char *buf, int len)
{
	return hspi_write(buf, len);
}


uint8_t nrc_hspi_crc7_compute (const uint8_t *data, size_t len)
{
	if (!data || len > 0x7fffffffU)
		return 0;
	return hspi_crc7(data, (int)len);
}

int nrc_hspi_get_queue_info (nrc_hspi_queue_info_t *info)
{
	if (!info || !g_hspi_info.active)
		return -1;

	memset(info, 0, sizeof(*info));
	info->target_to_host_slot_num = HSPI_TXQ_SLOT_NUM();
	info->target_to_host_slot_size = HSPI_TXQ_SLOT_SIZE();
	info->target_to_host_available = HSPI_TXQ_SLOT_COUNT();
	info->host_to_target_slot_num = HSPI_RXQ_SLOT_NUM();
	info->host_to_target_slot_size = HSPI_RXQ_SLOT_SIZE();
	info->host_to_target_available = HSPI_RXQ_SLOT_COUNT();
	return 0;
}

/* Call while holding the same host lock as read/write. Reads and clears the
 * target EIRQ latch before the host rearms a level-triggered GPIO interrupt. */
int nrc_hspi_refresh_status(void)
{
    return HSPI_ACTIVE() && !diagnostics.faulted ? hspi_status_update() : -1;
}

int nrc_hspi_get_diagnostics(nrc_hspi_diagnostics_t *out)
{
    if (!out) return -1;
    *out = diagnostics;
    return 0;
}
