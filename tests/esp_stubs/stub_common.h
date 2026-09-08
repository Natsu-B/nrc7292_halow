#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define IRAM_ATTR
#define ESP_INTR_FLAG_LOWMED 0
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_SPIRAM 4
#define MALLOC_CAP_DMA 8
#define SPI_DMA_CH_AUTO 0
#define SPI2_HOST 2
#define GPIO_NUM_NC (-1)
#define tskNO_AFFINITY (-1)
#define portMAX_DELAY UINT32_MAX
#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define ESP_OK 0
#define ESP_FAIL (-1)
#define ESP_ERR_INVALID_ARG (-2)
#define ESP_ERR_INVALID_STATE (-3)
#define ESP_ERR_NO_MEM (-4)
#define ESP_ERR_TIMEOUT (-5)
#define ESP_ERR_INVALID_SIZE (-6)
#define ESP_ERR_NOT_FOUND (-7)
#define ESP_LOGD(tag, fmt, ...) do { (void)(tag); if(0) fprintf(stderr,fmt,##__VA_ARGS__); } while(0)
#define ESP_LOGI(tag, fmt, ...) do { (void)(tag); if(0) fprintf(stderr,fmt,##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, fmt, ...) do { (void)(tag); if(0) fprintf(stderr,fmt,##__VA_ARGS__); } while(0)
#define ESP_LOGE(tag, fmt, ...) do { (void)(tag); if(0) fprintf(stderr,fmt,##__VA_ARGS__); } while(0)
#define ESP_ERROR_CHECK_WITHOUT_ABORT(x) do { (void)(x); } while (0)
#define portYIELD_FROM_ISR() do {} while (0)

typedef int esp_err_t;
typedef int gpio_num_t;
typedef int spi_host_device_t;
typedef uint32_t TickType_t;
typedef unsigned UBaseType_t;
typedef int BaseType_t;
typedef uint32_t EventBits_t;
typedef void *TaskHandle_t;
typedef void *SemaphoreHandle_t;
typedef void *EventGroupHandle_t;
typedef void *QueueHandle_t;
typedef int gpio_int_type_t;

enum { GPIO_MODE_INPUT = 1, GPIO_MODE_OUTPUT = 2 };
enum { GPIO_PULLUP_DISABLE = 0, GPIO_PULLUP_ENABLE = 1 };
enum { GPIO_PULLDOWN_DISABLE = 0, GPIO_PULLDOWN_ENABLE = 1 };
enum { GPIO_INTR_DISABLE = 0, GPIO_INTR_NEGEDGE = 1, GPIO_INTR_POSEDGE = 2, GPIO_INTR_HIGH_LEVEL = 3, GPIO_INTR_LOW_LEVEL = 4 };

typedef struct {
    uint64_t pin_bit_mask;
    int mode;
    int pull_up_en;
    int pull_down_en;
    gpio_int_type_t intr_type;
} gpio_config_t;

typedef void *spi_device_handle_t;
typedef struct {
    int mosi_io_num;
    int miso_io_num;
    int sclk_io_num;
    int quadwp_io_num;
    int quadhd_io_num;
    int max_transfer_sz;
} spi_bus_config_t;
typedef struct {
    int clock_speed_hz;
    int mode;
    int spics_io_num;
    int queue_size;
} spi_device_interface_config_t;
typedef struct {
    size_t length;
    size_t rxlength;
    const void *tx_buffer;
    void *rx_buffer;
} spi_transaction_t;

esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(gpio_num_t gpio, void (*handler)(void *), void *arg);
esp_err_t gpio_isr_handler_remove(gpio_num_t gpio);
esp_err_t spi_bus_initialize(spi_host_device_t host, const spi_bus_config_t *config, int dma);
esp_err_t spi_bus_add_device(spi_host_device_t host, const spi_device_interface_config_t *config, spi_device_handle_t *out);
esp_err_t spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *transaction);
esp_err_t spi_bus_remove_device(spi_device_handle_t device);
esp_err_t spi_bus_free(spi_host_device_t host);

SemaphoreHandle_t xSemaphoreCreateMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t timeout);
BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore);
void vSemaphoreDelete(SemaphoreHandle_t semaphore);
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t bits, BaseType_t clear, BaseType_t all, TickType_t timeout);
void vEventGroupDelete(EventGroupHandle_t group);
BaseType_t xTaskCreate(void (*task)(void *), const char *name, uint32_t stack, void *arg, UBaseType_t priority, TaskHandle_t *out);
void vTaskDelay(TickType_t ticks);
void vTaskDelayUntil(TickType_t *last, TickType_t increment);
TickType_t xTaskGetTickCount(void);
void vTaskDelete(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t timeout);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *woken);
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueSendToBack(QueueHandle_t queue, const void *item, TickType_t timeout);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t timeout);
BaseType_t xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

void *heap_caps_malloc(size_t size, unsigned caps);
void *heap_caps_calloc(size_t n, size_t size, unsigned caps);
size_t heap_caps_get_free_size(unsigned caps);
size_t heap_caps_get_largest_free_block(unsigned caps);
int64_t esp_timer_get_time(void);
uint32_t esp_random(void);
const char *esp_err_to_name(esp_err_t error);
void esp_restart(void);

#ifndef BIT0
#define BIT0 (1U << 0)
#define BIT1 (1U << 1)
#endif
#ifndef ESP_ERR_NOT_SUPPORTED
#define ESP_ERR_NOT_SUPPORTED (-8)
#endif
#define ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED (-9)
#define ESP_ERROR_CHECK(x) do { (void)(x); } while (0)

typedef struct esp_netif_obj esp_netif_t;
typedef void *esp_netif_iodriver_handle;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip,netmask,gw; } esp_netif_ip_info_t;
typedef enum { ESP_NETIF_DHCP_CLIENT=1,ESP_NETIF_FLAG_GARP=8,ESP_NETIF_FLAG_EVENT_IP_MODIFIED=16 } esp_netif_flags_t;
typedef struct { esp_netif_flags_t flags; uint8_t mac[6]; const esp_netif_ip_info_t *ip_info; uint32_t get_ip_event,lost_ip_event; const char *if_key,*if_desc; int route_prio; void *bridge_info; } esp_netif_inherent_config_t;
typedef struct { esp_err_t (*post_attach)(esp_netif_t*,esp_netif_iodriver_handle); esp_netif_t *netif; } esp_netif_driver_base_t;
typedef struct { void *handle; esp_err_t(*transmit)(void*,void*,size_t); esp_err_t(*transmit_wrap)(void*,void*,size_t,void*); void(*driver_free_rx_buffer)(void*,void*); esp_err_t(*driver_set_mac_filter)(void*,const uint8_t*,size_t,bool); } esp_netif_driver_ifconfig_t;
typedef struct { const esp_netif_inherent_config_t *base; const esp_netif_driver_ifconfig_t *driver; const void *stack; } esp_netif_config_t;
#define ESP_NETIF_INHERENT_DEFAULT_ETH() ((esp_netif_inherent_config_t){.flags=ESP_NETIF_DHCP_CLIENT,.if_key="ETH",.if_desc="eth",.route_prio=50})
#define ESP_NETIF_NETSTACK_DEFAULT_ETH ((const void*)1)
esp_err_t esp_netif_init(void); esp_err_t esp_event_loop_create_default(void); esp_netif_t *esp_netif_new(const esp_netif_config_t*); void esp_netif_destroy(esp_netif_t*); esp_err_t esp_netif_attach(esp_netif_t*,esp_netif_iodriver_handle); esp_err_t esp_netif_set_driver_config(esp_netif_t*,const esp_netif_driver_ifconfig_t*); esp_err_t esp_netif_receive(esp_netif_t*,void*,size_t,void*); esp_err_t esp_netif_set_mac(esp_netif_t*,const uint8_t*); void esp_netif_action_start(void*,void*,int32_t,void*); void esp_netif_action_stop(void*,void*,int32_t,void*); void esp_netif_action_connected(void*,void*,int32_t,void*); void esp_netif_action_disconnected(void*,void*,int32_t,void*); esp_err_t esp_netif_dhcpc_stop(esp_netif_t*); esp_err_t esp_netif_str_to_ip4(const char*,esp_ip4_addr_t*); esp_err_t esp_netif_set_ip_info(esp_netif_t*,const esp_netif_ip_info_t*); esp_err_t esp_netif_get_ip_info(esp_netif_t*,esp_netif_ip_info_t*);
BaseType_t xQueueOverwrite(QueueHandle_t queue,const void *item); UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue); BaseType_t xQueuePeek(QueueHandle_t queue,void *item,TickType_t timeout);

esp_err_t nvs_flash_init(void);
uint32_t esp_get_free_heap_size(void);
typedef struct {uint32_t size;} esp_partition_t;
#define ESP_PARTITION_TYPE_DATA 1
const esp_partition_t* esp_partition_find_first(int type,int subtype,const char* label);
esp_err_t esp_partition_read(const esp_partition_t*,size_t offset,void* dst,size_t length);

#define BIT2 (1U<<2)
#define BIT3 (1U<<3)
#define GPIO_IS_VALID_GPIO(x) ((x)>=0 && (x)<=48)
#define GPIO_IS_VALID_OUTPUT_GPIO(x) GPIO_IS_VALID_GPIO(x)
esp_err_t gpio_intr_disable(gpio_num_t);
esp_err_t gpio_intr_enable(gpio_num_t);
esp_err_t gpio_set_intr_type(gpio_num_t,gpio_int_type_t);
EventBits_t xEventGroupGetBits(EventGroupHandle_t);

void vTaskSuspend(TaskHandle_t task);
