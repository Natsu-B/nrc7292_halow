#pragma once
/* Syntax stand-in. The real firmware builds against ESP-IDF's ping_sock.h. */
#include "stub_common.h"
typedef void *esp_ping_handle_t;
typedef struct { uint32_t addr; } ip_addr_t;
#define ip_addr_set_ip4_u32(target,value) ((target)->addr=(value))
typedef struct {uint32_t count,interval_ms,timeout_ms,interface;ip_addr_t target_addr;} esp_ping_config_t;
#define ESP_PING_DEFAULT_CONFIG() ((esp_ping_config_t){.count=5,.interval_ms=1000,.timeout_ms=1000})
typedef struct {void (*on_ping_end)(esp_ping_handle_t,void*);void *cb_args;} esp_ping_callbacks_t;
enum {ESP_PING_PROF_REQUEST,ESP_PING_PROF_REPLY,ESP_PING_PROF_DURATION};
esp_err_t esp_ping_get_profile(esp_ping_handle_t,int,void*,uint32_t);
esp_err_t esp_ping_delete_session(esp_ping_handle_t);
esp_err_t esp_ping_new_session(const esp_ping_config_t*,const esp_ping_callbacks_t*,esp_ping_handle_t*);
esp_err_t esp_ping_start(esp_ping_handle_t);
