/* SPDX-License-Identifier: MIT */
#include "nrc_l2_protocol.h"

#include <string.h>

#define HELLO_WIRE_SIZE 14U
#define WIFI_CONFIG_WIRE_SIZE_LEGACY 110U
#define WIFI_CONFIG_WIRE_SIZE_V04 112U
#define WIFI_CONFIG_WIRE_SIZE_V05 120U
#define WIFI_CONFIG_WIRE_SIZE 132U
#define CONNECT_REQUEST_WIRE_SIZE 4U
#define COMMAND_RESULT_WIRE_SIZE 4U
#define WIFI_EVENT_WIRE_SIZE 2U
#define LINK_STATS_WIRE_SIZE_LEGACY 37U
#define LINK_STATS_WIRE_SIZE 38U

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static void put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static uint32_t get_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

uint16_t nrc_l2_crc16_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 0x8000U) ? (uint16_t)((crc << 1) ^ 0x1021U) : (uint16_t)(crc << 1);
    }
    return crc;
}

uint32_t nrc_l2_crc32_ieee(const uint8_t *data, size_t length)
{
    static const uint32_t table[16] = {
        0x00000000U,0x1db71064U,0x3b6e20c8U,0x26d930acU,
        0x76dc4190U,0x6b6b51f4U,0x4db26158U,0x5005713cU,
        0xedb88320U,0xf00f9344U,0xd6d6a3e8U,0xcb61b38cU,
        0x9b64c2b0U,0x86d3d2d4U,0xa00ae278U,0xbdbdf21cU,
    };
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ data[i]) & 0x0fU] ^ (crc >> 4);
        crc = table[(crc ^ (data[i] >> 4)) & 0x0fU] ^ (crc >> 4);
    }
    return ~crc;
}

int nrc_l2_message_encode(uint8_t type, uint16_t flags, uint16_t sequence,
                          const void *payload, uint16_t payload_length,
                          uint8_t *output, size_t capacity, size_t *output_length)
{
    if (!output || !output_length || payload_length > NRC_L2_MAX_PAYLOAD ||
        (payload_length && !payload) || capacity < NRC_L2_HEADER_SIZE + payload_length) return -1;
    output[0] = NRC_L2_MAGIC_0; output[1] = NRC_L2_MAGIC_1;
    output[2] = NRC_L2_PROTOCOL_VERSION; output[3] = type;
    put_u16(output + 4, flags); put_u16(output + 6, sequence); put_u16(output + 8, payload_length);
    put_u16(output + 10, nrc_l2_crc16_ccitt(output, 10));
    put_u32(output + 12, nrc_l2_crc32_ieee((const uint8_t *)payload, payload_length));
    if (payload_length) memcpy(output + NRC_L2_HEADER_SIZE, payload, payload_length);
    *output_length = NRC_L2_HEADER_SIZE + payload_length;
    return 0;
}

void nrc_l2_stream_decoder_init(nrc_l2_stream_decoder_t *d) { if (d) memset(d, 0, sizeof(*d)); }

static void consume(nrc_l2_stream_decoder_t *d, size_t n)
{
    if (n >= d->used) d->used = 0;
    else { memmove(d->buffer, d->buffer + n, d->used - n); d->used -= n; }
}

int nrc_l2_stream_decoder_feed(nrc_l2_stream_decoder_t *d, const uint8_t *data, size_t length,
                               nrc_l2_message_callback_t callback, void *context)
{
    if (!d || (!data && length) || !callback) return -1;
    int delivered = 0;
    while (length) {
        size_t free_space = sizeof(d->buffer) - d->used;
        if (!free_space) { consume(d, 1); d->framing_errors++; free_space = 1; }
        size_t chunk = length < free_space ? length : free_space;
        memcpy(d->buffer + d->used, data, chunk); d->used += chunk; data += chunk; length -= chunk;
        for (;;) {
            /* Resynchronize in one linear scan instead of memmoving the whole
             * decoder buffer for every damaged byte. Preserve a trailing byte
             * that may be the first half of the next record's magic. */
            size_t skip = 0;
            while (d->used - skip >= 2 &&
                   (d->buffer[skip] != NRC_L2_MAGIC_0 || d->buffer[skip + 1] != NRC_L2_MAGIC_1)) ++skip;
            if (skip) { consume(d, skip); d->framing_errors += (uint32_t)skip; }
            if (d->used < NRC_L2_HEADER_SIZE) break;
            uint16_t payload_length = get_u16(d->buffer + 8);
            if (d->buffer[2] != NRC_L2_PROTOCOL_VERSION || payload_length > NRC_L2_MAX_PAYLOAD ||
                get_u16(d->buffer + 10) != nrc_l2_crc16_ccitt(d->buffer, 10)) {
                consume(d, 1); d->framing_errors++; continue;
            }
            size_t total = NRC_L2_HEADER_SIZE + payload_length;
            if (d->used < total) break;
            const uint8_t *payload = d->buffer + NRC_L2_HEADER_SIZE;
            if (get_u32(d->buffer + 12) != nrc_l2_crc32_ieee(payload, payload_length)) {
                consume(d, total); d->crc_errors++; continue;
            }
            nrc_l2_message_view_t view = {
                .type = d->buffer[3], .flags = get_u16(d->buffer + 4), .sequence = get_u16(d->buffer + 6),
                .payload = payload, .payload_length = payload_length,
            };
            callback(&view, context); delivered++; consume(d, total);
        }
    }
    return delivered;
}

size_t nrc_l2_hello_encode(const nrc_l2_hello_t *v, uint8_t *o, size_t c)
{
    if (!v || !o || c < HELLO_WIRE_SIZE) return 0;
    put_u32(o, v->features); put_u16(o + 4, v->max_ethernet_frame); put_u16(o + 6, v->minimum_stats_period_ms);
    memcpy(o + 8, v->mac, 6); return HELLO_WIRE_SIZE;
}
bool nrc_l2_hello_decode(const uint8_t *d, size_t l, nrc_l2_hello_t *v)
{
    if (!d || !v || l != HELLO_WIRE_SIZE) return false;
    v->features = get_u32(d); v->max_ethernet_frame = get_u16(d + 4); v->minimum_stats_period_ms = get_u16(d + 6);
    memcpy(v->mac, d + 8, 6); return true;
}

size_t nrc_l2_wifi_config_encode(const nrc_l2_wifi_config_t *v, uint8_t *o, size_t c)
{
    if (!v || !o || c < WIFI_CONFIG_WIRE_SIZE ||
        (v->channel_bandwidth_mhz != 0U && v->channel_bandwidth_mhz != 1U &&
         v->channel_bandwidth_mhz != 2U && v->channel_bandwidth_mhz != 4U)) {
        return 0;
    }
    if (v->duty_cycle_enabled &&
        (v->duty_window_us == 0U || v->duty_duration_us == 0U ||
         v->duty_duration_us > v->duty_window_us ||
         v->duty_margin_us >= v->duty_duration_us)) {
        return 0;
    }

    const size_t sl = strnlen(v->ssid, 33);
    const size_t pl = strnlen(v->password, 65);
    if (sl == 0U || sl > 32U || pl > 64U) {
        return 0;
    }

    memset(o, 0, WIFI_CONFIG_WIRE_SIZE);
    o[0] = (uint8_t)v->country[0];
    o[1] = (uint8_t)v->country[1];
    o[2] = (uint8_t)v->security;
    o[3] = v->rate_control ? NRC_L2_WIFI_FLAG_RATE_CONTROL : 0U;
    o[4] = (uint8_t)v->tx_power_dbm;
    o[5] = v->sae_pwe;
    put_u16(o + 6, v->stats_period_ms);
    put_u32(o + 8, v->channel_frequency_khz);
    o[12] = (uint8_t)sl;
    o[13] = (uint8_t)pl;
    memcpy(o + 14, v->ssid, sl);
    memcpy(o + 46, v->password, pl);
    o[110] = v->channel_bandwidth_mhz;

    if (v->bssid_valid) {
        o[111] |= NRC_L2_WIFI_CONFIG_FLAG_BSSID_LOCK;
        memcpy(o + 112, v->bssid, sizeof(v->bssid));
    }
    if (v->duty_cycle_enabled) {
        o[111] |= NRC_L2_WIFI_CONFIG_FLAG_DUTY_CYCLE;
        put_u32(o + 120, v->duty_window_us);
        put_u32(o + 124, v->duty_duration_us);
        put_u32(o + 128, v->duty_margin_us);
    }
    return WIFI_CONFIG_WIRE_SIZE;
}

bool nrc_l2_wifi_config_decode(const uint8_t *d, size_t l, nrc_l2_wifi_config_t *v)
{
    if (!d || !v ||
        (l != WIFI_CONFIG_WIRE_SIZE_LEGACY && l != WIFI_CONFIG_WIRE_SIZE_V04 &&
         l != WIFI_CONFIG_WIRE_SIZE_V05 && l != WIFI_CONFIG_WIRE_SIZE) ||
        d[12] == 0U || d[12] > 32U || d[13] > 64U) {
        return false;
    }

    memset(v, 0, sizeof(*v));
    v->country[0] = (char)d[0];
    v->country[1] = (char)d[1];
    v->country[2] = '\0';
    v->security = (nrc_l2_security_t)d[2];
    v->rate_control = (d[3] & NRC_L2_WIFI_FLAG_RATE_CONTROL) != 0U;
    v->tx_power_dbm = (int8_t)d[4];
    v->sae_pwe = d[5];
    v->stats_period_ms = get_u16(d + 6);
    v->channel_frequency_khz = get_u32(d + 8);
    v->channel_bandwidth_mhz = l >= WIFI_CONFIG_WIRE_SIZE_V04 ? d[110] : 0U;
    if (v->channel_bandwidth_mhz != 0U && v->channel_bandwidth_mhz != 1U &&
        v->channel_bandwidth_mhz != 2U && v->channel_bandwidth_mhz != 4U) {
        return false;
    }

    if (l >= WIFI_CONFIG_WIRE_SIZE_V05) {
        const uint8_t allowed_flags = l == WIFI_CONFIG_WIRE_SIZE
                                          ? (NRC_L2_WIFI_CONFIG_FLAG_BSSID_LOCK |
                                             NRC_L2_WIFI_CONFIG_FLAG_DUTY_CYCLE)
                                          : NRC_L2_WIFI_CONFIG_FLAG_BSSID_LOCK;
        if ((d[111] & (uint8_t)~allowed_flags) != 0U) {
            return false;
        }
        v->bssid_valid = (d[111] & NRC_L2_WIFI_CONFIG_FLAG_BSSID_LOCK) != 0U;
        if (v->bssid_valid) {
            memcpy(v->bssid, d + 112, sizeof(v->bssid));
        }
    }

    if (l == WIFI_CONFIG_WIRE_SIZE &&
        (d[111] & NRC_L2_WIFI_CONFIG_FLAG_DUTY_CYCLE) != 0U) {
        v->duty_cycle_enabled = true;
        v->duty_window_us = get_u32(d + 120);
        v->duty_duration_us = get_u32(d + 124);
        v->duty_margin_us = get_u32(d + 128);
        if (v->duty_window_us == 0U || v->duty_duration_us == 0U ||
            v->duty_duration_us > v->duty_window_us ||
            v->duty_margin_us >= v->duty_duration_us) {
            return false;
        }
    }

    memcpy(v->ssid, d + 14, d[12]);
    v->ssid[d[12]] = '\0';
    memcpy(v->password, d + 46, d[13]);
    v->password[d[13]] = '\0';
    return true;
}

size_t nrc_l2_connect_request_encode(const nrc_l2_connect_request_t *v,
                                     uint8_t *o,
                                     size_t c)
{
    if (!v || !o || c < CONNECT_REQUEST_WIRE_SIZE || v->timeout_ms == 0U) return 0;
    put_u32(o, v->timeout_ms);
    return CONNECT_REQUEST_WIRE_SIZE;
}

bool nrc_l2_connect_request_decode(const uint8_t *d,
                                   size_t l,
                                   nrc_l2_connect_request_t *v)
{
    if (!d || !v || l != CONNECT_REQUEST_WIRE_SIZE) return false;
    v->timeout_ms = get_u32(d);
    return v->timeout_ms != 0U;
}

size_t nrc_l2_command_result_encode(const nrc_l2_command_result_t *v, uint8_t *o, size_t c)
{ if (!v || !o || c < 4) return 0; put_u16(o,v->request_sequence); put_u16(o+2,(uint16_t)v->result); return 4; }
bool nrc_l2_command_result_decode(const uint8_t *d,size_t l,nrc_l2_command_result_t *v)
{ if(!d||!v||l!=4)return false; v->request_sequence=get_u16(d); v->result=(int16_t)get_u16(d+2); return true; }
size_t nrc_l2_wifi_event_encode(const nrc_l2_wifi_event_t *v,uint8_t *o,size_t c)
{ if(!v||!o||c<2)return 0; o[0]=v->event;o[1]=v->reason;return 2; }
bool nrc_l2_wifi_event_decode(const uint8_t *d,size_t l,nrc_l2_wifi_event_t *v)
{ if(!d||!v||l!=2)return false;v->event=d[0];v->reason=d[1];return true; }

size_t nrc_l2_link_stats_encode(const nrc_l2_link_stats_t *v, uint8_t *o, size_t c)
{
    if (!v || !o || c < LINK_STATS_WIRE_SIZE) return 0;
    put_u32(o, v->sample_time_ms);
    o[4]=(uint8_t)v->rssi_last_dbm;o[5]=(uint8_t)v->rssi_average_dbm;o[6]=(uint8_t)v->data_rssi_last_dbm;o[7]=(uint8_t)v->data_rssi_average_dbm;
    o[8]=(uint8_t)v->snr_db;o[9]=v->tx_mcs;o[10]=v->rx_mcs;o[11]=v->connected;
    put_u32(o+12,v->modem_tx_space);put_u32(o+16,v->modem_rx_space);put_u32(o+20,v->host_to_radio_drops);put_u32(o+24,v->radio_to_host_drops);
    put_u16(o+28,v->host_to_radio_queue_depth);put_u16(o+30,v->radio_to_host_queue_depth);put_u16(o+32,v->host_to_radio_queue_high_water);put_u16(o+34,v->radio_to_host_queue_high_water);
    o[36]=v->channel_bandwidth_mhz;
    o[37]=v->duty_tx_available ? 1U : 0U;
    return LINK_STATS_WIRE_SIZE;
}

bool nrc_l2_link_stats_decode(const uint8_t *d, size_t l, nrc_l2_link_stats_t *v)
{
    if (!d || !v || (l != LINK_STATS_WIRE_SIZE_LEGACY && l != LINK_STATS_WIRE_SIZE)) return false;
    memset(v, 0, sizeof(*v));
    v->sample_time_ms = get_u32(d);
    v->rssi_last_dbm=(int8_t)d[4];v->rssi_average_dbm=(int8_t)d[5];v->data_rssi_last_dbm=(int8_t)d[6];v->data_rssi_average_dbm=(int8_t)d[7];v->snr_db=(int8_t)d[8];v->tx_mcs=d[9];v->rx_mcs=d[10];v->connected=d[11];
    v->modem_tx_space=get_u32(d+12);v->modem_rx_space=get_u32(d+16);v->host_to_radio_drops=get_u32(d+20);v->radio_to_host_drops=get_u32(d+24);
    v->host_to_radio_queue_depth=get_u16(d+28);v->radio_to_host_queue_depth=get_u16(d+30);v->host_to_radio_queue_high_water=get_u16(d+32);v->radio_to_host_queue_high_water=get_u16(d+34);v->channel_bandwidth_mhz=d[36];
    /* v0.5 targets had no explicit bit. Preserve compatibility by treating it as available. */
    v->duty_tx_available = l == LINK_STATS_WIRE_SIZE ? (d[37] != 0U) : 1U;
    return true;
}

