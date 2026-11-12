#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "lizhi_app_logic.h"
#include "lizhi_client.h"
#include "lizhi_follow.h"
#include "lizhi_settings.h"

typedef enum { LIZHI_JOB_NETWORK, LIZHI_JOB_PAGE, LIZHI_JOB_MEDIA, LIZHI_JOB_FOLLOWS,
    LIZHI_JOB_BOOT, LIZHI_JOB_SETTINGS, LIZHI_JOB_AUTH_CREATE, LIZHI_JOB_AUTH_POLL,
    LIZHI_JOB_WIFI_SCAN, LIZHI_JOB_WIFI_CONNECT, LIZHI_JOB_SYNC } lizhi_job_kind_t;
typedef struct { char ssid[33]; bool secure; } lizhi_wifi_ap_t;
typedef struct {
    uint32_t generation;
    lizhi_job_kind_t kind;
    uint32_t page;
    lizhi_client_result_t result;
    size_t count;
    /* 每个请求只有一种结果，避免授权与设置常驻占用播放堆。 */
    union {
        struct { lizhi_row_t rows[LIZHI_VIEW_MAX_ITEMS]; char url[LIZHI_URL_BYTES + 1]; };
        lizhi_follow_view_t follows;
        lizhi_settings_t settings;
        lizhi_auth_qr_t qr;
        struct { lizhi_auth_state_t auth_state; lizhi_account_t account; };
        lizhi_wifi_ap_t aps[12];
    };
    size_t ap_count, library_total;
    bool cached;
    bool wifi_previous_restored;
    esp_err_t wifi_restore_error;
} lizhi_job_result_t;

/* 全局唯一、常驻API worker；结果来自两个固定槽位，不持有UI指针。 */
bool lizhi_adapter_init(QueueHandle_t *results);
void lizhi_adapter_release(lizhi_job_result_t *result);
uint32_t lizhi_adapter_submit(lizhi_job_kind_t kind, uint32_t page, const char *episode_id);
uint32_t lizhi_adapter_submit_for(lizhi_job_kind_t kind, uint32_t page, const char *episode_id,
                                  const char *author, const char *cursor);
/* 同步取消并等待worker完成；仅应用任务调用，不在按键/LVGL回调中调用。 */
void lizhi_adapter_cancel(void);
bool lizhi_adapter_online(void);
uint32_t lizhi_adapter_command(lizhi_job_kind_t kind,const lizhi_settings_t *settings,
    const char *ticket,const char *ssid,const char *password);
