#include <assert.h>
#include <string.h>

#include "nrc_l2_protocol.h"

static int delivered;

static void callback(const nrc_l2_message_view_t *message, void *context)
{
    (void)context;
    assert(message->type == NRC_L2_MSG_ETH_TX);
    assert(message->payload_length == 5);
    assert(memcmp(message->payload, "hello", 5) == 0);
    delivered++;
}

int main(void)
{
    assert(nrc_l2_crc32_ieee((const uint8_t*)"123456789",9)==0xcbf43926U);
    assert(nrc_l2_crc16_ccitt((const uint8_t*)"123456789",9)==0x29b1U);
    uint8_t message[NRC_L2_MAX_MESSAGE];
    size_t message_length = 0;
    assert(nrc_l2_message_encode(NRC_L2_MSG_ETH_TX, 3, 7, "hello", 5,
                                 message, sizeof(message), &message_length) == 0);

    nrc_l2_stream_decoder_t decoder;
    nrc_l2_stream_decoder_init(&decoder);
    assert(nrc_l2_stream_decoder_feed(&decoder, message, 3, callback, NULL) == 0);
    assert(nrc_l2_stream_decoder_feed(&decoder, message + 3,
                                      message_length - 3, callback, NULL) == 1);
    assert(delivered == 1);

    nrc_l2_wifi_config_t config = {
        .country = "JP",
        .security = NRC_L2_SECURITY_WPA2_PSK,
        .rate_control = true,
        .tx_power_dbm = 20,
        .stats_period_ms = 50,
        .channel_frequency_khz = 923500,
        .sae_pwe = 2,
        .channel_bandwidth_mhz = 1,
        .bssid_valid = true,
        .bssid = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
        .ssid = "abc",
        .password = "12345678",
        .duty_cycle_enabled = true,
        .duty_window_us = 3600000000U,
        .duty_duration_us = 360000000U,
        .duty_margin_us = 0U,
    };
    nrc_l2_wifi_config_t decoded = {0};
    uint8_t payload[160];
    size_t payload_length = nrc_l2_wifi_config_encode(&config, payload,
                                                       sizeof(payload));
    assert(payload_length == 132U);
    assert(nrc_l2_wifi_config_decode(payload, payload_length, &decoded));
    assert(strcmp(decoded.ssid, "abc") == 0);
    assert(decoded.rate_control);
    assert(decoded.channel_frequency_khz == 923500U);
    assert(decoded.channel_bandwidth_mhz == 1U);
    assert(decoded.bssid_valid);
    assert(memcmp(decoded.bssid, config.bssid, 6) == 0);
    assert(decoded.duty_cycle_enabled);
    assert(decoded.duty_window_us == 3600000000U);
    assert(decoded.duty_duration_us == 360000000U);
    assert(decoded.duty_margin_us == 0U);

    /* v0.4's 110-byte payload remains decodable and means follow-the-AP. */
    memset(&decoded, 0xff, sizeof(decoded));
    assert(nrc_l2_wifi_config_decode(payload, 110U, &decoded));
    assert(decoded.channel_bandwidth_mhz == 0U);
    assert(!decoded.bssid_valid);
    assert(!decoded.duty_cycle_enabled);

    /* The intermediate 112-byte payload keeps bandwidth but has no BSSID. */
    memset(&decoded, 0xff, sizeof(decoded));
    assert(nrc_l2_wifi_config_decode(payload, 112U, &decoded));
    assert(decoded.channel_bandwidth_mhz == 1U);
    assert(!decoded.bssid_valid);
    assert(!decoded.duty_cycle_enabled);

    /* v0.5 had BSSID locking but no native duty-cycle fields. */
    payload[111] &= (uint8_t)~NRC_L2_WIFI_CONFIG_FLAG_DUTY_CYCLE;
    memset(&decoded, 0xff, sizeof(decoded));
    assert(nrc_l2_wifi_config_decode(payload, 120U, &decoded));
    assert(decoded.bssid_valid);
    assert(!decoded.duty_cycle_enabled);

    config.channel_bandwidth_mhz = 3;
    assert(nrc_l2_wifi_config_encode(&config, payload, sizeof(payload)) == 0U);
    config.channel_bandwidth_mhz = 1;
    config.duty_margin_us = config.duty_duration_us;
    assert(nrc_l2_wifi_config_encode(&config, payload, sizeof(payload)) == 0U);
    config.duty_margin_us = 0U;

    nrc_l2_connect_request_t connect_request = {.timeout_ms = 4750U};
    nrc_l2_connect_request_t decoded_connect = {0};
    payload_length = nrc_l2_connect_request_encode(&connect_request, payload,
                                                    sizeof(payload));
    assert(payload_length == 4U);
    assert(nrc_l2_connect_request_decode(payload, payload_length,
                                         &decoded_connect));
    assert(decoded_connect.timeout_ms == 4750U);
    connect_request.timeout_ms = 0U;
    assert(nrc_l2_connect_request_encode(&connect_request, payload,
                                         sizeof(payload)) == 0U);

    nrc_l2_link_stats_t stats = {
        .sample_time_ms = 123,
        .rssi_last_dbm = -100,
        .rssi_average_dbm = -99,
        .data_rssi_last_dbm = -101,
        .data_rssi_average_dbm = -100,
        .snr_db = 4,
        .tx_mcs = 1,
        .rx_mcs = 2,
        .connected = 1,
        .modem_tx_space = 55,
        .channel_bandwidth_mhz = 1,
        .duty_tx_available = 1,
    };
    nrc_l2_link_stats_t decoded_stats = {0};
    payload_length = nrc_l2_link_stats_encode(&stats, payload, sizeof(payload));
    assert(payload_length == 38U);
    assert(nrc_l2_link_stats_decode(payload, payload_length, &decoded_stats));
    assert(decoded_stats.channel_bandwidth_mhz == 1U);
    assert(decoded_stats.rssi_average_dbm == -99);
    assert(decoded_stats.duty_tx_available == 1U);
    assert(nrc_l2_link_stats_decode(payload, 37U, &decoded_stats));
    assert(decoded_stats.duty_tx_available == 1U);

    return 0;
}
