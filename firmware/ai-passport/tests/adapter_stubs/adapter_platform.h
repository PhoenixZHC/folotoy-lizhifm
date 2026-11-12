#pragma once
/* 主机验收专用，生产构建不包含这些平台替身。 */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "nvs.h"

#define ESP_LOGE(tag, ...) ((void)(tag), (void)snprintf(NULL, 0, __VA_ARGS__))
#define CONFIG_LIZHI_WIFI_NAMESPACE "old_wifi"
#define CONFIG_LIZHI_ACCOUNT_USER_ID ""
#define CONFIG_LIZHI_PUBLIC_AUTHOR_ID "123"
#define pdMS_TO_TICKS(ms) (ms)
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
typedef void *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned, size_t);
int xQueueReceive(QueueHandle_t, void *, unsigned);
int xQueueSend(QueueHandle_t, const void *, unsigned);
int xQueueOverwrite(QueueHandle_t, const void *);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
unsigned uxTaskGetStackHighWaterMark(void *);
void vTaskDelay(unsigned);

typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
#define WIFI_EVENT ((esp_event_base_t)1)
#define IP_EVENT ((esp_event_base_t)2)
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_DISCONNECTED 1
#define IP_EVENT_STA_GOT_IP 2
#define WIFI_IF_STA 0
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_STA 0
#define WIFI_PS_NONE 0
#define WIFI_SCAN_TYPE_ACTIVE 0
#define WIFI_AUTH_OPEN 0
#define SNTP_OPMODE_POLL 0
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct {
    struct { uint8_t ssid[32], password[64]; struct { bool capable; } pmf_cfg; } sta;
} wifi_config_t;
typedef struct { unsigned unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef struct { bool show_hidden; unsigned scan_type; } wifi_scan_config_t;
typedef struct { uint8_t ssid[33]; unsigned authmode; } wifi_ap_record_t;
esp_err_t nvs_get_str(nvs_handle_t, const char *, char *, size_t *);
esp_err_t nvs_flash_init(void);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
void *esp_netif_create_default_wifi_sta(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t,
    void (*)(void *, esp_event_base_t, int32_t, void *), void *, esp_event_handler_instance_t *);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_ps(int);
esp_err_t esp_wifi_set_config(int, const wifi_config_t *);
esp_err_t esp_wifi_get_config(int, wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *, bool);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *, wifi_ap_record_t *);
int64_t esp_timer_get_time(void);
bool esp_sntp_enabled(void);
void esp_sntp_setoperatingmode(int);
void esp_sntp_setservername(int, const char *);
void esp_sntp_init(void);
const char *esp_err_to_name(esp_err_t);
