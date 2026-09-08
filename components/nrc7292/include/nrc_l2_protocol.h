/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NRC_L2_PROTOCOL_VERSION              1U
#define NRC_L2_HEADER_SIZE                  16U
#define NRC_L2_MAX_ETHERNET_FRAME         1518U
#define NRC_L2_MAX_PAYLOAD                1600U
#define NRC_L2_MAX_MESSAGE (NRC_L2_HEADER_SIZE + NRC_L2_MAX_PAYLOAD)
#define NRC_L2_MAGIC_0                    0x4eU /* N */
#define NRC_L2_MAGIC_1                    0x42U /* B */

#define NRC_L2_FEATURE_HSPI_LOOPBACK          (1UL << 9)

#define NRC_L2_FEATURE_ETHERNET_BRIDGE       (1UL << 0)
#define NRC_L2_FEATURE_PERIODIC_LINK_STATS   (1UL << 1)
#define NRC_L2_FEATURE_INTERNAL_RATE_CONTROL (1UL << 2)
#define NRC_L2_FEATURE_HOST_OWNS_IP          (1UL << 3)
/* Data-plane ownership, not a claim that the SDK contains no lwIP code. */
#define NRC_L2_FEATURE_NO_TARGET_IP_STACK NRC_L2_FEATURE_HOST_OWNS_IP
#define NRC_L2_FEATURE_PRIORITY_QUEUES       (1UL << 4)
#define NRC_L2_FEATURE_CHANNEL_BW_CONFIG      (1UL << 5)
#define NRC_L2_FEATURE_BSSID_LOCK              (1UL << 6)
#define NRC_L2_FEATURE_BOUNDED_CONNECT         (1UL << 7)
#define NRC_L2_FEATURE_DUTY_CYCLE_GUARD         (1UL << 8)

#define NRC_L2_WIFI_FLAG_RATE_CONTROL        (1U << 0)
#define NRC_L2_WIFI_CONFIG_FLAG_BSSID_LOCK    (1U << 0)
#define NRC_L2_WIFI_CONFIG_FLAG_DUTY_CYCLE    (1U << 1)

typedef enum {
    NRC_L2_MSG_HELLO_REQ = 1,
    NRC_L2_MSG_HELLO_RSP,
    NRC_L2_MSG_WIFI_CONFIG,
    NRC_L2_MSG_CONNECT,
    NRC_L2_MSG_DISCONNECT,
    NRC_L2_MSG_WIFI_EVENT,
    NRC_L2_MSG_ETH_TX,
    NRC_L2_MSG_ETH_RX,
    NRC_L2_MSG_LINK_STATS,
    NRC_L2_MSG_HEARTBEAT,
    NRC_L2_MSG_COMMAND_RESULT,
    NRC_L2_MSG_ERROR,
    NRC_L2_MSG_LOOPBACK_REQ,
    NRC_L2_MSG_LOOPBACK_RSP,
} nrc_l2_message_type_t;

typedef enum {
    NRC_L2_WIFI_EVENT_DISCONNECTED = 0,
    NRC_L2_WIFI_EVENT_CONNECTED = 1,
} nrc_l2_wifi_event_id_t;

typedef enum {
    NRC_L2_SECURITY_OPEN = 0,
    NRC_L2_SECURITY_WPA2_PSK = 1,
    NRC_L2_SECURITY_WPA3_SAE = 2,
    NRC_L2_SECURITY_WPA3_OWE = 3,
} nrc_l2_security_t;

typedef enum {
    NRC_L2_RESULT_OK = 0,
    NRC_L2_RESULT_INVALID = -1,
    NRC_L2_RESULT_BUSY = -2,
    NRC_L2_RESULT_IO = -3,
    NRC_L2_RESULT_UNSUPPORTED = -4,
    NRC_L2_RESULT_AUTH = -5,
} nrc_l2_result_t;

typedef struct {
    uint32_t features;
    uint16_t max_ethernet_frame;
    uint16_t minimum_stats_period_ms;
    uint8_t mac[6];
} nrc_l2_hello_t;

typedef struct {
    char country[3];
    nrc_l2_security_t security;
    bool rate_control;
    int8_t tx_power_dbm;
    uint16_t stats_period_ms;
    uint32_t channel_frequency_khz; /* 0: firmware scans normally */
    uint8_t sae_pwe;
    uint8_t channel_bandwidth_mhz; /* 0: follow AP, otherwise 1/2/4 */
    bool bssid_valid;              /* locks reassociation to the known AP */
    uint8_t bssid[6];
    char ssid[33];
    char password[65];

    /* NRC7292 native airtime guard. Units are microseconds, matching SDK 1.7. */
    bool duty_cycle_enabled;
    uint32_t duty_window_us;
    uint32_t duty_duration_us;
    uint32_t duty_margin_us;
} nrc_l2_wifi_config_t;

typedef struct {
    uint32_t timeout_ms;
} nrc_l2_connect_request_t;

typedef struct {
    uint16_t request_sequence;
    int16_t result;
} nrc_l2_command_result_t;

typedef struct {
    uint8_t event;
    uint8_t reason;
} nrc_l2_wifi_event_t;

typedef struct {
    uint32_t sample_time_ms;
    int8_t rssi_last_dbm;
    int8_t rssi_average_dbm;
    int8_t data_rssi_last_dbm;
    int8_t data_rssi_average_dbm;
    int8_t snr_db;
    uint8_t tx_mcs;
    uint8_t rx_mcs;
    uint8_t connected;
    uint32_t modem_tx_space;
    uint32_t modem_rx_space;
    uint32_t host_to_radio_drops;
    uint32_t radio_to_host_drops;
    uint16_t host_to_radio_queue_depth;
    uint16_t radio_to_host_queue_depth;
    uint16_t host_to_radio_queue_high_water;
    uint16_t radio_to_host_queue_high_water;
    uint8_t channel_bandwidth_mhz;
    uint8_t duty_tx_available;
} nrc_l2_link_stats_t;

typedef struct {
    uint8_t type;
    uint16_t flags;
    uint16_t sequence;
    const uint8_t *payload;
    uint16_t payload_length;
} nrc_l2_message_view_t;

typedef void (*nrc_l2_message_callback_t)(const nrc_l2_message_view_t *message, void *context);

typedef struct {
    uint8_t buffer[NRC_L2_MAX_MESSAGE];
    size_t used;
    uint32_t framing_errors;
    uint32_t crc_errors;
} nrc_l2_stream_decoder_t;

uint16_t nrc_l2_crc16_ccitt(const uint8_t *data, size_t length);
uint32_t nrc_l2_crc32_ieee(const uint8_t *data, size_t length);

int nrc_l2_message_encode(uint8_t type,
                          uint16_t flags,
                          uint16_t sequence,
                          const void *payload,
                          uint16_t payload_length,
                          uint8_t *output,
                          size_t output_capacity,
                          size_t *output_length);

void nrc_l2_stream_decoder_init(nrc_l2_stream_decoder_t *decoder);
int nrc_l2_stream_decoder_feed(nrc_l2_stream_decoder_t *decoder,
                               const uint8_t *data,
                               size_t length,
                               nrc_l2_message_callback_t callback,
                               void *context);

size_t nrc_l2_hello_encode(const nrc_l2_hello_t *value, uint8_t *output, size_t capacity);
bool nrc_l2_hello_decode(const uint8_t *data, size_t length, nrc_l2_hello_t *value);
size_t nrc_l2_wifi_config_encode(const nrc_l2_wifi_config_t *value, uint8_t *output, size_t capacity);
bool nrc_l2_wifi_config_decode(const uint8_t *data, size_t length, nrc_l2_wifi_config_t *value);
size_t nrc_l2_connect_request_encode(const nrc_l2_connect_request_t *value,
                                             uint8_t *output,
                                             size_t capacity);
bool nrc_l2_connect_request_decode(const uint8_t *data,
                                   size_t length,
                                   nrc_l2_connect_request_t *value);
size_t nrc_l2_command_result_encode(const nrc_l2_command_result_t *value, uint8_t *output, size_t capacity);
bool nrc_l2_command_result_decode(const uint8_t *data, size_t length, nrc_l2_command_result_t *value);
size_t nrc_l2_wifi_event_encode(const nrc_l2_wifi_event_t *value, uint8_t *output, size_t capacity);
bool nrc_l2_wifi_event_decode(const uint8_t *data, size_t length, nrc_l2_wifi_event_t *value);
size_t nrc_l2_link_stats_encode(const nrc_l2_link_stats_t *value, uint8_t *output, size_t capacity);
bool nrc_l2_link_stats_decode(const uint8_t *data, size_t length, nrc_l2_link_stats_t *value);

#ifdef __cplusplus
}
#endif
