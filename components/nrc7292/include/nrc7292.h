/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "nrc_l2_protocol.h"
#include "nrc-hspi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NRC7292_DEFAULT_SPI_CLOCK_HZ 5000000
#define NRC7292_DEFAULT_STATS_PERIOD_MS 50
#define NRC7292_DEFAULT_POWER_ON_SETTLE_MS 20

typedef struct nrc7292_device *nrc7292_handle_t;

typedef enum {
    NRC7292_EIRQ_DISABLED = 0,
    NRC7292_EIRQ_ACTIVE_HIGH_LEVEL,
    NRC7292_EIRQ_ACTIVE_LOW_LEVEL,
    NRC7292_EIRQ_RISING_EDGE,
    NRC7292_EIRQ_FALLING_EDGE,
} nrc7292_eirq_mode_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t pin_sclk;
    gpio_num_t pin_mosi;
    gpio_num_t pin_miso;
    gpio_num_t pin_cs;
    gpio_num_t pin_cs_idle; /* Other wired CS, always driven high. */
    gpio_num_t pin_eirq;
    gpio_num_t pin_reset;

    /* Optional load-switch/enable pin for a strict RF-silent standby. */
    gpio_num_t pin_power_enable;
    bool power_enable_active_high;
    uint32_t power_on_settle_ms;

    int spi_clock_hz;
    bool reset_active_low;
    nrc7292_eirq_mode_t eirq_mode;
    size_t rx_buffer_size;
    UBaseType_t rx_task_priority;
    UBaseType_t tx_task_priority;
    uint32_t rx_task_stack_size;
    uint32_t tx_task_stack_size;
    uint16_t control_queue_depth;
    uint16_t data_queue_depth;
} nrc7292_config_t;

#define NRC7292_CONFIG_DEFAULT()                                                \
    {                                                                           \
        .spi_host = SPI2_HOST,                                                   \
        .pin_sclk = GPIO_NUM_NC,                                                 \
        .pin_mosi = GPIO_NUM_NC,                                                 \
        .pin_miso = GPIO_NUM_NC,                                                 \
        .pin_cs = GPIO_NUM_NC,                                                   \
        .pin_cs_idle = GPIO_NUM_NC,                                              \
        .pin_eirq = GPIO_NUM_NC,                                                 \
        .pin_reset = GPIO_NUM_NC,                                                \
        .pin_power_enable = GPIO_NUM_NC,                                         \
        .power_enable_active_high = true,                                        \
        .power_on_settle_ms = NRC7292_DEFAULT_POWER_ON_SETTLE_MS,                \
        .spi_clock_hz = NRC7292_DEFAULT_SPI_CLOCK_HZ,                            \
        .reset_active_low = false,                                                \
        .eirq_mode = NRC7292_EIRQ_ACTIVE_HIGH_LEVEL,                             \
        .rx_buffer_size = 4096,                                                  \
        .rx_task_priority = 18,                                                  \
        .tx_task_priority = 17,                                                  \
        .rx_task_stack_size = 6144,                                              \
        .tx_task_stack_size = 5120,                                              \
        .control_queue_depth = 8,                                                \
        .data_queue_depth = 24,                                                  \
    }

typedef nrc_l2_wifi_config_t nrc7292_wifi_config_t;
typedef struct {
    nrc_hspi_diagnostics_t native;
    nrc_hspi_queue_info_t queues;
    uint32_t irq_count;
    uint32_t irq_high_count;
    int eirq_level;
    bool eirq_enabled;
} nrc7292_diagnostics_t;
/* rx_task_priority/stack_size configure the single I/O task. tx_task_* fields
 * are retained for source compatibility but no separate TX task is created. */
esp_err_t nrc7292_get_diagnostics(nrc7292_handle_t handle, nrc7292_diagnostics_t *out);
/* Enable the wired active-high level interrupt from the I/O task after polling. */
esp_err_t nrc7292_enable_eirq(nrc7292_handle_t handle);
/* Echo over HSPI only: no association and no RF frame is emitted. */
esp_err_t nrc7292_loopback(nrc7292_handle_t handle, const void *data, size_t length, TickType_t timeout);

typedef struct {
    bool hspi_ready;
    bool wifi_connected;
    uint8_t mac[6];
    uint32_t features;
    nrc_l2_link_stats_t radio;
    uint32_t radio_stats_age_ms; /* UINT32_MAX until a target sample arrives. */
    uint64_t ethernet_tx_frames;
    uint64_t ethernet_tx_bytes;
    uint64_t ethernet_rx_frames;
    uint64_t ethernet_rx_bytes;
    uint32_t host_tx_queue_drops;
    uint32_t protocol_framing_errors;
    uint32_t protocol_crc_errors;
    uint16_t host_data_queue_depth;
} nrc7292_link_stats_t;

/*
 * Assert RESET and/or disable the external power gate without starting SPI.
 * At least one gate pin must be configured.  A hardware pull is still required
 * to guarantee silence during ESP32-P4 reset/ROM boot.
 */
esp_err_t nrc7292_force_radio_off(const nrc7292_config_t *config);

esp_err_t nrc7292_init(const nrc7292_config_t *config, nrc7292_handle_t *out);
/* Poll ID, signature and geometry without EIRQ or L2 HELLO. Retains selected bus. */
esp_err_t nrc7292_probe(nrc7292_handle_t handle, TickType_t timeout);
esp_err_t nrc7292_start(nrc7292_handle_t handle, TickType_t hello_timeout);
esp_err_t nrc7292_create_netif(nrc7292_handle_t handle, esp_netif_t **out_netif);
esp_err_t nrc7292_configure_wifi(nrc7292_handle_t handle,
                                 const nrc7292_wifi_config_t *config,
                                 TickType_t timeout);
esp_err_t nrc7292_connect(nrc7292_handle_t handle, TickType_t timeout);
esp_err_t nrc7292_disconnect(nrc7292_handle_t handle, TickType_t timeout);
esp_err_t nrc7292_get_link_stats(nrc7292_handle_t handle,
                                 nrc7292_link_stats_t *stats);
bool nrc7292_is_connected(nrc7292_handle_t handle);
/* Close application sockets/consumers first. Lifecycle operations are not
 * concurrent APIs. ESP_OK destroys the handle; ESP_ERR_TIMEOUT retains it.
 * stop() does not reset the NRC7292 or clear its Duty history. */
esp_err_t nrc7292_stop(nrc7292_handle_t handle);

#ifdef __cplusplus
}
#endif
