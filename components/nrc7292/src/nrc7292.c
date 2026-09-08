/* SPDX-License-Identifier: MIT
 * ESP-IDF adapter for the Newracom NRC7292 native HSPI slave interface.
 * One FreeRTOS I/O task owns all native queue/status transactions after startup.
 * An L2 record may span multiple native slots; it is never interleaved with
 * another record. Callers enqueue copies, so lwIP buffers may be released.
 */
#include "nrc7292.h"
#include "nrc-hspi.h"
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif_defaults.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define EV_HELLO BIT0
#define EV_COMMAND BIT1
#define EV_FAULT BIT2
#define EV_IO_EXIT BIT3
#define DATA_TTL_US 500000LL
#define IO_WATCHDOG_MS 100U
#define IO_POLL_MS 5U
#define IO_TX_QUANTUM (2U * (HSPI_SLOT_SIZE_MAX - HSPI_SLOT_HDR_SIZE))
static const char *TAG = "nrc_native_hspi";

typedef struct {
    int64_t deadline;
    size_t length;
    uint8_t bytes[NRC_L2_MAX_MESSAGE];
} tx_item_t;

struct nrc7292_device {
    esp_netif_driver_base_t netif_driver; /* esp_netif_attach ABI: first field */
    nrc7292_config_t config;
    spi_device_handle_t spi;
    bool bus_ready, irq_installed;
    uint8_t *spi_tx, *spi_rx, *rx_buffer;
    SemaphoreHandle_t state_mutex, command_mutex;
    QueueHandle_t control_q, data_q;
    EventGroupHandle_t events;
    TaskHandle_t io_task;
    atomic_bool running, hspi_ready, wifi_connected;
    atomic_bool irq_requested;
    atomic_uint irq_count;
    atomic_uint irq_high_count;
    uint16_t next_seq, pending_seq;
    uint8_t pending_type;
    int16_t pending_result;
    const uint8_t *pending_echo;
    size_t pending_echo_len;
    nrc_l2_stream_decoder_t decoder;
    nrc_l2_hello_t hello;
    nrc_l2_link_stats_t radio;
    int64_t radio_stats_received_us;
    nrc7292_diagnostics_t diag;
    uint64_t tx_frames, tx_bytes, rx_frames, rx_bytes;
    uint32_t tx_drops, crc_errors, framing_errors;
    esp_netif_t *netif;
    bool netif_started;
};
_Static_assert(offsetof(struct nrc7292_device, netif_driver) == 0, "netif ABI");
static struct nrc7292_device *s_active; /* native reference driver is single-instance */

static void lock(SemaphoreHandle_t m) { (void)xSemaphoreTake(m, portMAX_DELAY); }
static void unlock(SemaphoreHandle_t m) { (void)xSemaphoreGive(m); }
static TickType_t ms_ticks(uint32_t ms) {
    TickType_t t = pdMS_TO_TICKS(ms);
    return t ? t : 1;
}
static int64_t deadline_after(TickType_t ticks) {
    /* All public operations remain bounded, including a portMAX_DELAY caller. */
    uint64_t ms = (uint64_t)ticks * portTICK_PERIOD_MS;
    if (ms > 120000U) ms = 120000U;
    return esp_timer_get_time() + (int64_t)ms * 1000;
}
static TickType_t remaining(int64_t deadline) {
    int64_t us = deadline - esp_timer_get_time();
    return us <= 0 ? 0 : ms_ticks((uint32_t)((us + 999) / 1000));
}
static uint16_t sequence(struct nrc7292_device *d) {
    lock(d->state_mutex);
    if (++d->next_seq == 0) ++d->next_seq;
    uint16_t result = d->next_seq;
    unlock(d->state_mutex);
    return result;
}
static int hprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vprintf(fmt, ap); va_end(ap); return n;
}
static int spi_transfer(char *tx, char *rx, int length) {
    struct nrc7292_device *d = s_active;
    if (!d || !d->spi || length <= 0 || length > HSPI_XFER_LEN_MAX || (!tx && !rx)) return -1;
    /* Internal, DMA-capable, aligned bounce buffers. No stack/PSRAM pointers
     * reach the SPI DMA engine. Transactions preserve reference CS boundaries. */
    if (tx) memcpy(d->spi_tx, tx, (size_t)length);
    else memset(d->spi_tx, 0, (size_t)length);
    spi_transaction_t transfer = {
        .length = (size_t)length * 8U,
        .rxlength = rx ? (size_t)length * 8U : 0,
        .tx_buffer = d->spi_tx,
        .rx_buffer = rx ? d->spi_rx : NULL,
    };
    esp_err_t err = spi_device_polling_transmit(d->spi, &transfer);
    if (err == ESP_OK && rx) memcpy(rx, d->spi_rx, (size_t)length);
    return err == ESP_OK ? 0 : -1;
}
static enum HSPI_EIRQ_MODE target_irq(const nrc7292_config_t *c) {
    if (c->pin_eirq == GPIO_NUM_NC) return HSPI_EIRQ_MODE_NONE;
    switch (c->eirq_mode) {
    case NRC7292_EIRQ_ACTIVE_HIGH_LEVEL: return HSPI_EIRQ_MODE_HIGH;
    case NRC7292_EIRQ_ACTIVE_LOW_LEVEL: return HSPI_EIRQ_MODE_LOW;
    case NRC7292_EIRQ_RISING_EDGE: return HSPI_EIRQ_MODE_RISING;
    case NRC7292_EIRQ_FALLING_EDGE: return HSPI_EIRQ_MODE_FALLING;
    default: return HSPI_EIRQ_MODE_NONE;
    }
}
static bool irq_is_level(const nrc7292_config_t *c) {
    return c->eirq_mode == NRC7292_EIRQ_ACTIVE_HIGH_LEVEL ||
           c->eirq_mode == NRC7292_EIRQ_ACTIVE_LOW_LEVEL;
}
static gpio_int_type_t host_irq(nrc7292_eirq_mode_t mode) {
    switch (mode) {
    case NRC7292_EIRQ_ACTIVE_HIGH_LEVEL: return GPIO_INTR_HIGH_LEVEL;
    case NRC7292_EIRQ_ACTIVE_LOW_LEVEL: return GPIO_INTR_LOW_LEVEL;
    case NRC7292_EIRQ_RISING_EDGE: return GPIO_INTR_POSEDGE;
    case NRC7292_EIRQ_FALLING_EDGE: return GPIO_INTR_NEGEDGE;
    default: return GPIO_INTR_DISABLE;
    }
}
static void irq_isr(void *arg) {
    struct nrc7292_device *d = arg;
    BaseType_t wake = pdFALSE;
    /* No SPI, logging or allocation here. Mask level IRQ to avoid an ISR storm.
     * The I/O task drains queues/clears the native latch and rearms it. */
    if (irq_is_level(&d->config)) gpio_intr_disable(d->config.pin_eirq);
    atomic_fetch_add_explicit(&d->irq_count, 1U, memory_order_relaxed);
    if (gpio_get_level(d->config.pin_eirq))
        atomic_fetch_add_explicit(&d->irq_high_count, 1U, memory_order_relaxed);
    if (d->io_task) vTaskNotifyGiveFromISR(d->io_task, &wake);
    if (wake) portYIELD_FROM_ISR();
}
static esp_err_t configure_irq(struct nrc7292_device *d) {
    const nrc7292_config_t *c = &d->config;
    if (target_irq(c) == HSPI_EIRQ_MODE_NONE) return ESP_OK;
    gpio_config_t gpio = {
        .pin_bit_mask = 1ULL << (unsigned)c->pin_eirq,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&gpio);
    if (err != ESP_OK) return err;
    /* Deliberately not ESP_INTR_FLAG_IRAM: atomics and helper code are not
     * promised cache-independent. Do not erase Flash while streaming. */
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
    err = gpio_isr_handler_add(c->pin_eirq, irq_isr, d);
    if (err == ESP_OK) {
        d->irq_installed = true;
        err = gpio_set_intr_type(c->pin_eirq, host_irq(c->eirq_mode));
    }
    return err;
}
static void rearm_irq(struct nrc7292_device *d) {
    if (!d->irq_installed) return;
    gpio_intr_enable(d->config.pin_eirq);
    /* Level-triggered hardware will immediately notify if the level asserted
     * between clearing the target latch and rearming. A 100-ms watchdog also
     * catches a broken/omitted interrupt wire. No IRQ edge is treated as credit. */
}
static esp_err_t set_output_level(gpio_num_t pin, int value) {
    if (pin == GPIO_NUM_NC) return ESP_OK;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;
    /* Set the output latch before enabling the driver, avoiding a reset glitch. */
    esp_err_t err = gpio_set_level(pin, value != 0);
    if (err != ESP_OK) return err;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << (unsigned)pin, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    return gpio_config(&config);
}
esp_err_t nrc7292_force_radio_off(const nrc7292_config_t *c) {
    if (!c) return ESP_ERR_INVALID_ARG;
    if (c->pin_reset == GPIO_NUM_NC && c->pin_power_enable == GPIO_NUM_NC) return ESP_ERR_NOT_SUPPORTED;
    esp_err_t err = set_output_level(c->pin_reset, !c->reset_active_low);
    if (err == ESP_OK) err = set_output_level(c->pin_power_enable, !c->power_enable_active_high);
    return err;
}
static esp_err_t power_up(struct nrc7292_device *d) {
    const nrc7292_config_t *c = &d->config;
    esp_err_t err = set_output_level(c->pin_reset, !c->reset_active_low);
    if (err == ESP_OK) err = set_output_level(c->pin_power_enable, c->power_enable_active_high);
    if (err != ESP_OK) return err;
    vTaskDelay(ms_ticks(c->power_on_settle_ms + 20U));
    err = set_output_level(c->pin_reset, c->reset_active_low);
    if (err == ESP_OK) vTaskDelay(ms_ticks(250));
    return err;
}
static void set_link(struct nrc7292_device *d, bool up) {
    lock(d->state_mutex);
    bool changed = atomic_load(&d->wifi_connected) != up;
    atomic_store(&d->wifi_connected, up);
    esp_netif_t *netif = d->netif_started ? d->netif : NULL;
    unlock(d->state_mutex);
    if (changed && netif) {
        if (up) esp_netif_action_connected(netif, 0, 0, NULL);
        else esp_netif_action_disconnected(netif, 0, 0, NULL);
    }
}
static void snapshot_native(struct nrc7292_device *d) {
    nrc_hspi_diagnostics_t native;
    nrc_hspi_queue_info_t q = {0};
    nrc_hspi_get_diagnostics(&native);
    (void)nrc_hspi_get_queue_info(&q);
    lock(d->state_mutex);
    d->diag.native = native;
    d->diag.queues = q;
    d->diag.irq_count = atomic_load(&d->irq_count);
    d->diag.irq_high_count = atomic_load(&d->irq_high_count);
    d->diag.eirq_level = d->config.pin_eirq == GPIO_NUM_NC ? -1 : gpio_get_level(d->config.pin_eirq);
    d->diag.eirq_enabled = d->irq_installed;
    d->crc_errors = d->decoder.crc_errors;
    d->framing_errors = d->decoder.framing_errors;
    unlock(d->state_mutex);
}
static void handle_message(const nrc_l2_message_view_t *m, void *context) {
    struct nrc7292_device *d = context;
    switch (m->type) {
    case NRC_L2_MSG_HELLO_RSP: {
        nrc_l2_hello_t hello;
        if (!nrc_l2_hello_decode(m->payload, m->payload_length, &hello)) break;
        lock(d->state_mutex); d->hello = hello; unlock(d->state_mutex);
        xEventGroupSetBits(d->events, EV_HELLO);
        break;
    }
    case NRC_L2_MSG_COMMAND_RESULT: {
        nrc_l2_command_result_t result;
        if (!nrc_l2_command_result_decode(m->payload, m->payload_length, &result)) break;
        lock(d->state_mutex);
        if (d->pending_seq && result.request_sequence == d->pending_seq) {
            d->pending_result = result.result;
            xEventGroupSetBits(d->events, EV_COMMAND);
        }
        unlock(d->state_mutex);
        break;
    }
    case NRC_L2_MSG_LOOPBACK_RSP:
        lock(d->state_mutex);
        if (d->pending_type == NRC_L2_MSG_LOOPBACK_REQ && d->pending_seq == m->sequence) {
            d->pending_result = d->pending_echo && d->pending_echo_len == m->payload_length &&
                memcmp(d->pending_echo, m->payload, m->payload_length) == 0 ?
                NRC_L2_RESULT_OK : NRC_L2_RESULT_IO;
            xEventGroupSetBits(d->events, EV_COMMAND);
        }
        unlock(d->state_mutex);
        break;
    case NRC_L2_MSG_WIFI_EVENT: {
        nrc_l2_wifi_event_t e;
        if (nrc_l2_wifi_event_decode(m->payload, m->payload_length, &e))
            set_link(d, e.event == NRC_L2_WIFI_EVENT_CONNECTED);
        break;
    }
    case NRC_L2_MSG_LINK_STATS: {
        nrc_l2_link_stats_t stats;
        if (!nrc_l2_link_stats_decode(m->payload, m->payload_length, &stats)) break;
        lock(d->state_mutex); d->radio = stats; d->radio_stats_received_us = esp_timer_get_time(); unlock(d->state_mutex);
        set_link(d, stats.connected != 0); /* repairs a dropped state event */
        break;
    }
    case NRC_L2_MSG_ETH_RX: {
        if (m->payload_length < 14 || m->payload_length > NRC_L2_MAX_ETHERNET_FRAME) break;
        lock(d->state_mutex);
        esp_netif_t *netif = d->netif_started ? d->netif : NULL;
        unlock(d->state_mutex);
        if (!netif) break;
        void *buffer = malloc(m->payload_length);
        if (!buffer) break;
        memcpy(buffer, m->payload, m->payload_length);
        /* IDF v5.5.2 ethernetif_input consumes/frees the buffer also on drop.
         * No second free here. l2_buff is unused by the Ethernet stack. */
        if (esp_netif_receive(netif, buffer, m->payload_length, NULL) == ESP_OK) {
            lock(d->state_mutex); d->rx_frames++; d->rx_bytes += m->payload_length; unlock(d->state_mutex);
        }
        break;
    }
    default: break;
    }
}
static esp_err_t enqueue(struct nrc7292_device *d, bool control, uint8_t type,
                         uint16_t seq, const void *data, uint16_t size, int64_t deadline) {
    if (!atomic_load(&d->running) || !atomic_load(&d->hspi_ready)) return ESP_ERR_INVALID_STATE;
    tx_item_t *item = heap_caps_malloc(sizeof(*item), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!item) return ESP_ERR_NO_MEM;
    if (nrc_l2_message_encode(type, 0, seq, data, size, item->bytes, sizeof(item->bytes), &item->length)) {
        free(item); return ESP_ERR_INVALID_ARG;
    }
    item->deadline = deadline;
    if (xQueueSendToBack(control ? d->control_q : d->data_q, &item, 0) != pdPASS) {
        free(item); lock(d->state_mutex); d->tx_drops++; unlock(d->state_mutex);
        return ESP_ERR_NO_MEM;
    }
    xTaskNotifyGive(d->io_task);
    return ESP_OK;
}
static void io_task(void *arg) {
    struct nrc7292_device *d = arg;
    tx_item_t *active = NULL;
    size_t offset = 0;
    bool failed = false;
    while (atomic_load(&d->running)) {
        if (atomic_exchange(&d->irq_requested, false) && !d->irq_installed) {
            d->config.eirq_mode = NRC7292_EIRQ_ACTIVE_HIGH_LEVEL;
            if (nrc_hspi_set_eirq_mode(HSPI_EIRQ_MODE_HIGH) != 0 || configure_irq(d) != ESP_OK) {
                failed = true; break;
            }
        }
        if (nrc_hspi_refresh_status() != 0) { failed = true; break; }
        int count = nrc_hspi_read((char *)d->rx_buffer, (int)d->config.rx_buffer_size);
        if (count < 0) { failed = true; break; }
        if (count > 0) (void)nrc_l2_stream_decoder_feed(&d->decoder, d->rx_buffer, (size_t)count, handle_message, d);
        if (!active) {
            if (xQueueReceive(d->control_q, &active, 0) != pdPASS)
                (void)xQueueReceive(d->data_q, &active, 0);
            offset = 0;
        }
        bool progress = count > 0;
        if (active && esp_timer_get_time() >= active->deadline) {
            lock(d->state_mutex); d->tx_drops++; unlock(d->state_mutex);
            /* Dropping before the first byte is harmless; abandoning half a
             * record is not. Fail the session rather than replay uncertain data. */
            if (offset != 0) { failed = true; break; }
            free(active); active = NULL;
        }
        if (active) {
            size_t chunk = active->length - offset;
            if (chunk > IO_TX_QUANTUM) chunk = IO_TX_QUANTUM;
            int sent = nrc_hspi_write((char *)active->bytes + offset, (int)chunk);
            if (sent < 0 || (size_t)sent > chunk) { failed = true; break; }
            offset += (size_t)sent;
            progress |= sent > 0;
            if (offset == active->length) { free(active); active = NULL; }
        }
        snapshot_native(d);
        rearm_irq(d);
        /* RX always runs between TX quanta. Queue pressure does not starve
         * control responses. Interrupts are hints, native credits are authority. */
        if (!progress) {
            uint32_t wait_ms = active || uxQueueMessagesWaiting(d->control_q) || uxQueueMessagesWaiting(d->data_q) ?
                1U : (d->irq_installed ? IO_WATCHDOG_MS : IO_POLL_MS);
            (void)ulTaskNotifyTake(pdTRUE, ms_ticks(wait_ms));
        } else {
            vTaskDelay(1); /* bounded fairness to lower-priority app/Wi-Fi tasks */
        }
    }
    free(active);
    if (d->irq_installed) gpio_intr_disable(d->config.pin_eirq);
    snapshot_native(d);
    atomic_store(&d->hspi_ready, false);
    atomic_store(&d->running, false);
    if (failed) {
        ESP_LOGE(TAG, "HSPI session fault. Streaming stopped; no automatic target reset or replay.");
        xEventGroupSetBits(d->events, EV_FAULT);
    }
    set_link(d, false);
    nrc_hspi_close();
    xEventGroupSetBits(d->events, EV_IO_EXIT); /* no use of d after this line */
    /* Keep the TCB allocated until stop() joins us. Producers can still have a
     * notification in flight after a transport fault. No DMA/mutex is owned at
     * this point. stop() is allowed to delete this quiescent, suspended task. */
    vTaskSuspend(NULL);
    vTaskDelete(NULL);
}
static esp_err_t command(struct nrc7292_device *d, uint8_t type, const void *p,
                          uint16_t length, TickType_t timeout) {
    if (!d || !timeout || !atomic_load(&d->hspi_ready)) return ESP_ERR_INVALID_STATE;
    int64_t deadline = deadline_after(timeout);
    if (xSemaphoreTake(d->command_mutex, remaining(deadline)) != pdTRUE) return ESP_ERR_TIMEOUT;
    uint16_t seq = sequence(d);
    xEventGroupClearBits(d->events, EV_COMMAND);
    lock(d->state_mutex);
    d->pending_seq = seq; d->pending_type = type; d->pending_result = NRC_L2_RESULT_IO;
    d->pending_echo = type == NRC_L2_MSG_LOOPBACK_REQ ? p : NULL;
    d->pending_echo_len = length;
    unlock(d->state_mutex);
    esp_err_t err = enqueue(d, true, type, seq, p, length, deadline);
    if (err == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(d->events, EV_COMMAND | EV_FAULT, pdFALSE, pdFALSE, remaining(deadline));
        if (bits & EV_FAULT) err = ESP_FAIL;
        else if (!(bits & EV_COMMAND)) err = ESP_ERR_TIMEOUT;
        else {
            lock(d->state_mutex);
            err = d->pending_result == NRC_L2_RESULT_OK ? ESP_OK : ESP_FAIL;
            unlock(d->state_mutex);
        }
    }
    lock(d->state_mutex);
    d->pending_seq = 0; d->pending_type = 0; d->pending_echo = NULL; d->pending_echo_len = 0;
    unlock(d->state_mutex);
    xSemaphoreGive(d->command_mutex);
    return err;
}
static esp_err_t netif_tx(void *handle, void *buffer, size_t length) {
    struct nrc7292_device *d = handle;
    if (!d || !buffer || length < 14 || length > NRC_L2_MAX_ETHERNET_FRAME) return ESP_ERR_INVALID_ARG;
    if (!atomic_load(&d->wifi_connected)) return ESP_ERR_INVALID_STATE;
    esp_err_t err = enqueue(d, false, NRC_L2_MSG_ETH_TX, sequence(d), buffer,
                            (uint16_t)length, esp_timer_get_time() + DATA_TTL_US);
    if (err == ESP_OK) {
        lock(d->state_mutex); d->tx_frames++; d->tx_bytes += length; unlock(d->state_mutex);
    }
    return err;
}
static void netif_free_rx(void *handle, void *buffer) { (void)handle; free(buffer); }
static esp_err_t post_attach(esp_netif_t *netif, esp_netif_iodriver_handle handle) {
    struct nrc7292_device *d = (struct nrc7292_device *)handle;
    d->netif_driver.netif = netif;
    esp_netif_driver_ifconfig_t driver = {.handle = d, .transmit = netif_tx, .driver_free_rx_buffer = netif_free_rx};
    return esp_netif_set_driver_config(netif, &driver);
}
static void release_queue(QueueHandle_t queue) {
    if (!queue) return;
    tx_item_t *item;
    while (xQueueReceive(queue, &item, 0) == pdPASS) free(item);
    vQueueDelete(queue);
}
static void release(struct nrc7292_device *d) {
    if (d->irq_installed) { gpio_intr_disable(d->config.pin_eirq); gpio_isr_handler_remove(d->config.pin_eirq); }
    if (d->netif) {
        if (d->netif_started) esp_netif_action_stop(d->netif, 0, 0, NULL);
        esp_netif_destroy(d->netif);
    }
    if (d->spi) spi_bus_remove_device(d->spi);
    if (d->bus_ready) spi_bus_free(d->config.spi_host);
    (void)set_output_level(d->config.pin_cs, 1);
    (void)set_output_level(d->config.pin_cs_idle, 1);
    release_queue(d->control_q); release_queue(d->data_q);
    if (d->events) vEventGroupDelete(d->events);
    if (d->state_mutex) vSemaphoreDelete(d->state_mutex);
    if (d->command_mutex) vSemaphoreDelete(d->command_mutex);
    if (s_active == d) { nrc_hspi_close(); s_active = NULL; }
    free(d->spi_tx); free(d->spi_rx); free(d->rx_buffer); free(d);
}
static bool valid_config(const nrc7292_config_t *c) {
    if (!c || !GPIO_IS_VALID_OUTPUT_GPIO(c->pin_sclk) || !GPIO_IS_VALID_OUTPUT_GPIO(c->pin_mosi) ||
        !GPIO_IS_VALID_OUTPUT_GPIO(c->pin_cs) || !GPIO_IS_VALID_GPIO(c->pin_miso)) return false;
    if (c->pin_eirq != GPIO_NUM_NC && !GPIO_IS_VALID_GPIO(c->pin_eirq)) return false;
    if (c->pin_cs_idle != GPIO_NUM_NC && !GPIO_IS_VALID_OUTPUT_GPIO(c->pin_cs_idle)) return false;
    if (c->pin_reset != GPIO_NUM_NC && !GPIO_IS_VALID_OUTPUT_GPIO(c->pin_reset)) return false;
    if (c->pin_power_enable != GPIO_NUM_NC && !GPIO_IS_VALID_OUTPUT_GPIO(c->pin_power_enable)) return false;
    const gpio_num_t pins[] = {c->pin_sclk,c->pin_mosi,c->pin_miso,c->pin_cs,c->pin_cs_idle,c->pin_eirq,c->pin_reset,c->pin_power_enable};
    for (unsigned i = 0; i < sizeof(pins)/sizeof(pins[0]); ++i)
        for (unsigned j = i+1; j < sizeof(pins)/sizeof(pins[0]); ++j)
            if (pins[i] != GPIO_NUM_NC && pins[i] == pins[j]) return false;
    return c->spi_clock_hz >= 1000000 && c->spi_clock_hz <= 25000000 &&
        c->rx_buffer_size >= HSPI_SLOT_SIZE_MAX && c->rx_buffer_size <= 16384 &&
        c->control_queue_depth > 0 && c->control_queue_depth <= 64 &&
        c->data_queue_depth > 0 && c->data_queue_depth <= 64 &&
        c->power_on_settle_ms <= 5000;
}
esp_err_t nrc7292_init(const nrc7292_config_t *c, nrc7292_handle_t *out) {
    if (!out || !valid_config(c)) return ESP_ERR_INVALID_ARG;
    *out = NULL;
    if (s_active) return ESP_ERR_INVALID_STATE;
    struct nrc7292_device *d = calloc(1, sizeof(*d));
    if (!d) return ESP_ERR_NO_MEM;
    d->config = *c;
    atomic_init(&d->running, false); atomic_init(&d->hspi_ready, false);
    atomic_init(&d->wifi_connected, false); atomic_init(&d->irq_count, 0U);
    atomic_init(&d->irq_requested, false);
    atomic_init(&d->irq_high_count, 0U);
    d->spi_tx = heap_caps_malloc(HSPI_XFER_LEN_MAX, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    d->spi_rx = heap_caps_malloc(HSPI_XFER_LEN_MAX, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    d->rx_buffer = heap_caps_malloc(c->rx_buffer_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    d->state_mutex = xSemaphoreCreateMutex(); d->command_mutex = xSemaphoreCreateMutex();
    d->events = xEventGroupCreate();
    d->control_q = xQueueCreate(c->control_queue_depth, sizeof(tx_item_t *));
    d->data_q = xQueueCreate(c->data_queue_depth, sizeof(tx_item_t *));
    if (!d->spi_tx || !d->spi_rx || !d->rx_buffer || !d->state_mutex || !d->command_mutex ||
        !d->events || !d->control_q || !d->data_q) { release(d); return ESP_ERR_NO_MEM; }
    /* Latches go high before either CS becomes an output or joins the SPI bus. */
    esp_err_t err = set_output_level(c->pin_cs, 1);
    if (err == ESP_OK) err = set_output_level(c->pin_cs_idle, 1);
    if (err == ESP_OK && c->pin_eirq != GPIO_NUM_NC) {
        gpio_config_t input = {
            .pin_bit_mask = 1ULL << (unsigned)c->pin_eirq, .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        err = gpio_config(&input);
    }
    if (err != ESP_OK) { release(d); return err; }
    vTaskDelay(ms_ticks(10));
    spi_bus_config_t bus = {
        .mosi_io_num=c->pin_mosi, .miso_io_num=c->pin_miso, .sclk_io_num=c->pin_sclk,
        .quadwp_io_num=-1, .quadhd_io_num=-1, .max_transfer_sz=HSPI_XFER_LEN_MAX,
    };
    err = spi_bus_initialize(c->spi_host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) { release(d); return err; }
    d->bus_ready = true;
    spi_device_interface_config_t device = {
        .clock_speed_hz=c->spi_clock_hz, .mode=0, .spics_io_num=c->pin_cs, .queue_size=1,
    };
    err = spi_bus_add_device(c->spi_host, &device, &d->spi);
    if (err != ESP_OK) { release(d); return err; }
    d->netif_driver.post_attach = post_attach;
    nrc_l2_stream_decoder_init(&d->decoder);
    *out = d;
    return ESP_OK;
}
esp_err_t nrc7292_probe(nrc7292_handle_t d, TickType_t timeout) {
    if (!d || !timeout || (s_active && s_active != d) || d->io_task) return ESP_ERR_INVALID_STATE;
    bool already_open = s_active == d;
    int64_t deadline = deadline_after(timeout);
    s_active = d;
    /* Re-read chip/signature/queues on every polling probe, without resetting
     * an already open target or treating cached identity as live evidence. */
    esp_err_t err = already_open ? ESP_OK : power_up(d);
    if (err != ESP_OK) { s_active = NULL; return err; }
    hspi_ops_t ops = {.printf=hprintf, .delay=NULL, .spi_transfer=spi_transfer};
    int opened = -1;
    do {
        opened = nrc_hspi_open(&ops, HSPI_EIRQ_MODE_NONE);
        if (!opened) break;
        vTaskDelay(ms_ticks(100));
    } while (remaining(deadline));
    snapshot_native(d);
    if (opened) { nrc_hspi_close(); s_active = NULL; return ESP_ERR_TIMEOUT; }
    return ESP_OK;
}
esp_err_t nrc7292_start(nrc7292_handle_t d, TickType_t timeout) {
    if (!d || !timeout || d->io_task) return ESP_ERR_INVALID_STATE;
    int64_t deadline = deadline_after(timeout);
    esp_err_t err = nrc7292_probe(d, timeout);
    if (err != ESP_OK) return err;
    if (nrc_hspi_set_eirq_mode(target_irq(&d->config)) != 0) return ESP_FAIL;
    atomic_store(&d->running, true); atomic_store(&d->hspi_ready, true);
    xEventGroupClearBits(d->events, EV_IO_EXIT | EV_FAULT | EV_HELLO | EV_COMMAND);
    /* IRQ is installed while task is inactive. A notification before the task
     * handle exists is harmless because its first iteration always polls. */
    err = configure_irq(d);
    if (err != ESP_OK || xTaskCreate(io_task, "nrc_hspi_io", d->config.rx_task_stack_size, d,
                                    d->config.rx_task_priority, &d->io_task) != pdPASS) {
        atomic_store(&d->running, false); atomic_store(&d->hspi_ready, false);
        if (d->irq_installed) { gpio_intr_disable(d->config.pin_eirq); gpio_isr_handler_remove(d->config.pin_eirq); d->irq_installed=false; }
        nrc_hspi_close(); s_active=NULL;
        return err != ESP_OK ? err : ESP_ERR_NO_MEM;
    }
    err = enqueue(d, true, NRC_L2_MSG_HELLO_REQ, sequence(d), NULL, 0, deadline);
    if (err == ESP_OK) {
        EventBits_t bits = xEventGroupWaitBits(d->events, EV_HELLO | EV_FAULT, pdFALSE, pdFALSE, remaining(deadline));
        if (bits & EV_FAULT) err = ESP_FAIL;
        else if (!(bits & EV_HELLO)) err = ESP_ERR_TIMEOUT;
    }
    if (err == ESP_OK) {
        const uint32_t required = NRC_L2_FEATURE_ETHERNET_BRIDGE | NRC_L2_FEATURE_HOST_OWNS_IP |
                                  NRC_L2_FEATURE_PERIODIC_LINK_STATS | NRC_L2_FEATURE_INTERNAL_RATE_CONTROL;
        if ((d->hello.features & required) != required || d->hello.max_ethernet_frame < NRC_L2_MAX_ETHERNET_FRAME)
            err = ESP_ERR_NOT_SUPPORTED;
    }
    if (err != ESP_OK) {
        atomic_store(&d->running, false); xTaskNotifyGive(d->io_task);
        /* stop() owns cooperative joining/freeing; do not destroy caller's handle here. */
    }
    return err;
}
esp_err_t nrc7292_create_netif(nrc7292_handle_t d, esp_netif_t **out) {
    if (!d || !out || !atomic_load(&d->hspi_ready)) return ESP_ERR_INVALID_STATE;
    if (d->netif) { *out=d->netif; return ESP_OK; }
    esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
    base.if_key="HALOW_L2"; base.if_desc="halow"; base.route_prio=80;
    /* Stage1 uses static IP. Prevent DHCP traffic before the caller configures it. */
    base.flags &= ~ESP_NETIF_DHCP_CLIENT;
    memcpy(base.mac, d->hello.mac, 6);
    esp_netif_config_t config = {.base=&base, .driver=NULL, .stack=ESP_NETIF_NETSTACK_DEFAULT_ETH};
    esp_netif_t *netif = esp_netif_new(&config);
    if (!netif) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_netif_attach(netif, d);
    if (err == ESP_OK) err = esp_netif_set_mac(netif, d->hello.mac);
    if (err != ESP_OK) { esp_netif_destroy(netif); return err; }
    esp_netif_action_start(netif, 0, 0, NULL);
    lock(d->state_mutex); d->netif=netif; d->netif_started=true; unlock(d->state_mutex);
    if (atomic_load(&d->wifi_connected)) esp_netif_action_connected(netif, 0, 0, NULL);
    *out=netif; return ESP_OK;
}
esp_err_t nrc7292_configure_wifi(nrc7292_handle_t d, const nrc7292_wifi_config_t *c, TickType_t timeout) {
    if (!d || !c) return ESP_ERR_INVALID_ARG;
    uint8_t data[160]; size_t n=nrc_l2_wifi_config_encode(c,data,sizeof(data));
    return n ? command(d,NRC_L2_MSG_WIFI_CONFIG,data,(uint16_t)n,timeout) : ESP_ERR_INVALID_ARG;
}
esp_err_t nrc7292_connect(nrc7292_handle_t d, TickType_t timeout) {
    if (!d || (uint64_t)timeout*portTICK_PERIOD_MS < 1250U) return ESP_ERR_INVALID_ARG;
    uint64_t ms=(uint64_t)timeout*portTICK_PERIOD_MS-250U;
    nrc_l2_connect_request_t request = {.timeout_ms=(uint32_t)(ms>60000U?60000U:ms)};
    uint8_t data[4]; size_t n=nrc_l2_connect_request_encode(&request,data,sizeof(data));
    return command(d,NRC_L2_MSG_CONNECT,data,(uint16_t)n,timeout);
}
esp_err_t nrc7292_disconnect(nrc7292_handle_t d, TickType_t timeout) {
    return d ? command(d,NRC_L2_MSG_DISCONNECT,NULL,0,timeout) : ESP_ERR_INVALID_ARG;
}
esp_err_t nrc7292_loopback(nrc7292_handle_t d, const void *data, size_t length, TickType_t timeout) {
    if (!d || !data || length<1 || length>NRC_L2_MAX_ETHERNET_FRAME) return ESP_ERR_INVALID_ARG;
    lock(d->state_mutex); bool supported=(d->hello.features & NRC_L2_FEATURE_HSPI_LOOPBACK)!=0; unlock(d->state_mutex);
    if(!supported)return ESP_ERR_NOT_SUPPORTED;
    return command(d,NRC_L2_MSG_LOOPBACK_REQ,data,(uint16_t)length,timeout);
}
esp_err_t nrc7292_get_diagnostics(nrc7292_handle_t d, nrc7292_diagnostics_t *out) {
    if (!d || !out) return ESP_ERR_INVALID_ARG;
    lock(d->state_mutex); *out=d->diag; unlock(d->state_mutex); return ESP_OK;
}
esp_err_t nrc7292_enable_eirq(nrc7292_handle_t d) {
    if (!d || d->config.pin_eirq == GPIO_NUM_NC || !atomic_load(&d->hspi_ready))
        return ESP_ERR_INVALID_STATE;
    atomic_store(&d->irq_requested, true);
    xTaskNotifyGive(d->io_task);
    return ESP_OK;
}
esp_err_t nrc7292_get_link_stats(nrc7292_handle_t d, nrc7292_link_stats_t *s) {
    if (!d || !s) return ESP_ERR_INVALID_ARG;
    lock(d->state_mutex); memset(s,0,sizeof(*s));
    s->hspi_ready=atomic_load(&d->hspi_ready); s->wifi_connected=atomic_load(&d->wifi_connected);
    memcpy(s->mac,d->hello.mac,6); s->features=d->hello.features; s->radio=d->radio;
    int64_t age_ms=(esp_timer_get_time()-d->radio_stats_received_us)/1000;
    s->radio_stats_age_ms=!d->radio_stats_received_us || age_ms>UINT32_MAX ? UINT32_MAX : (uint32_t)age_ms;
    s->ethernet_tx_frames=d->tx_frames; s->ethernet_tx_bytes=d->tx_bytes;
    s->ethernet_rx_frames=d->rx_frames; s->ethernet_rx_bytes=d->rx_bytes;
    s->host_tx_queue_drops=d->tx_drops;
    s->protocol_framing_errors=d->framing_errors; s->protocol_crc_errors=d->crc_errors;
    s->host_data_queue_depth=(uint16_t)uxQueueMessagesWaiting(d->data_q);
    unlock(d->state_mutex); return ESP_OK;
}
bool nrc7292_is_connected(nrc7292_handle_t d) { return d && atomic_load(&d->wifi_connected); }
esp_err_t nrc7292_stop(nrc7292_handle_t d) {
    if (!d) return ESP_OK;
    /* Caller must have stopped its socket producer. Never force-delete a task
     * that could own DMA buffers, a native transaction, or an IDF mutex. */
    if (d->io_task) {
        atomic_store(&d->running,false);
        if (!(xEventGroupGetBits(d->events) & EV_IO_EXIT)) xTaskNotifyGive(d->io_task);
        if (!(xEventGroupWaitBits(d->events,EV_IO_EXIT,pdFALSE,pdFALSE,ms_ticks(3000)) & EV_IO_EXIT))
            return ESP_ERR_TIMEOUT; /* retain handle/storage, allow later retry */
        vTaskDelete(d->io_task); /* EV_IO_EXIT proves no DMA/driver lock is owned */
        d->io_task=NULL;
    }
    /* No target reset here: host teardown must not erase target Duty history. */
    release(d); return ESP_OK;
}
