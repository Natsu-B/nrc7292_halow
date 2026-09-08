#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef void* QueueHandle_t;typedef void* TaskHandle_t;typedef uint32_t TickType_t;typedef int BaseType_t;typedef unsigned UBaseType_t;
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(x) ((TickType_t)(x))
void vTaskDelay(TickType_t);TickType_t xTaskGetTickCount(void);BaseType_t xTaskCreate(void(*)(void*),const char*,uint32_t,void*,UBaseType_t,TaskHandle_t*);QueueHandle_t xQueueCreate(UBaseType_t,UBaseType_t);BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t);BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t);BaseType_t xQueueOverwrite(QueueHandle_t,const void*);UBaseType_t uxQueueMessagesWaiting(QueueHandle_t);

#define taskENTER_CRITICAL() do {} while (0)
#define taskEXIT_CRITICAL() do {} while (0)

void hal_uart_printf(const char*,...);
