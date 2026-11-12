#include "lizhi_direct_adapter.h"
#include "lizhi_list_loader.h"
#include "lizhi_library.h"
#include "wifi_provisioning_logic.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    uint32_t generation;
    lizhi_job_kind_t kind;
    uint32_t page;
    char episode_id[LIZHI_ID_BYTES + 1];
    char author_id[LIZHI_ID_BYTES + 1];
    char cursor[LIZHI_FOLLOW_CURSOR_BYTES + 1];
    lizhi_settings_t settings;
    char ticket[LIZHI_AUTH_TOKEN_BYTES+1], ssid[33], password[65];
} request_t;

static const char *TAG = "lizhi_adapter";
static QueueHandle_t requests, results, free_results;
static lizhi_job_result_t result_pool[2];
static atomic_uint generation;
static atomic_uint last_submitted, last_completed;
static atomic_bool got_ip;
static bool network_initialized;
static bool network_attempted;
static esp_err_t initialization_error;
static esp_event_handler_instance_t wifi_event, ip_event;
static bool wifi_configured;
static atomic_uint disconnect_reason;
static atomic_bool reconnect_enabled;
static atomic_uint reconnect_attempts;
static atomic_llong reconnect_deadline;

static void on_network(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&got_ip, true);atomic_store(&reconnect_attempts,0);atomic_store(&reconnect_deadline,0);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&got_ip, false);
        const wifi_event_sta_disconnected_t *event = data;
        atomic_store(&disconnect_reason,event?event->reason:0);
        ESP_LOGW(TAG, "Wi-Fi disconnected reason=%u", event ? event->reason : 0);
        /* 热点重启往往超过三次扫描；最多等待30秒，避免先于音频恢复窗口放弃。 */
        int64_t now=esp_timer_get_time();
        if(!atomic_load(&reconnect_deadline))atomic_store(&reconnect_deadline,now+30000000);
        if(atomic_load(&reconnect_enabled)&&now<atomic_load(&reconnect_deadline)&&atomic_fetch_add(&reconnect_attempts,1)<10){
            esp_err_t e=esp_wifi_connect();
            if(e!=ESP_OK)ESP_LOGW(TAG,"Wi-Fi reconnect failed: %s",esp_err_to_name(e));
        }
    }
}

static bool cancelled(void *context)
{
    const request_t *r = context;
    return atomic_load(&generation) != r->generation;
}

static esp_err_t load_wifi(wifi_config_t *wifi)
{
    nvs_handle_t nvs;
    esp_err_t e=nvs_open("lizhi_wifi",NVS_READONLY,&nvs);
    if(e==ESP_OK){
        size_t bytes=sizeof(*wifi);
        e=nvs_get_blob(nvs,"station_v1",wifi,&bytes);nvs_close(nvs);
        return e==ESP_OK&&bytes==sizeof(*wifi)&&wifi->sta.ssid[0]?ESP_OK:ESP_ERR_INVALID_RESPONSE;
    }
    if(e!=ESP_ERR_NVS_NOT_FOUND)return e;
    e=nvs_open(CONFIG_LIZHI_WIFI_NAMESPACE,NVS_READONLY,&nvs);
    if(e!=ESP_OK)return e;
    char ssid[33]={0},password[65]={0};
    size_t ssid_size=sizeof(ssid),password_size=sizeof(password);
    e=nvs_get_str(nvs,"ssid",ssid,&ssid_size);
    if(e==ESP_OK){
        esp_err_t pe=nvs_get_str(nvs,"password",password,&password_size);
        if(pe!=ESP_OK&&pe!=ESP_ERR_NVS_NOT_FOUND)e=pe;
    }
    nvs_close(nvs);
    if(e==ESP_OK&&!ssid[0])e=ESP_ERR_INVALID_RESPONSE;
    if(e==ESP_OK){
        memcpy(wifi->sta.ssid,ssid,strlen(ssid));
        memcpy(wifi->sta.password,password,strlen(password));
    }
    memset(password,0,sizeof(password));
    return e;
}
static esp_err_t initialize_network(bool use_saved)
{
    esp_err_t e=nvs_flash_init();
    if(e!=ESP_OK)return e; /* 不自动擦除NVS。 */
    wifi_config_t wifi={0};
    wifi_configured=false;
    if(use_saved){
        e=load_wifi(&wifi);
        if(e!=ESP_OK&&e!=ESP_ERR_NVS_NOT_FOUND){memset(&wifi,0,sizeof(wifi));return e;}
        wifi_configured=e==ESP_OK;
    }
    /* 外设初始化中途失败时明确要求重启，不重复建立部分初始化的资源。 */
    network_attempted = true;
    e = esp_netif_init();
    if (e == ESP_OK) e = esp_event_loop_create_default();
    if (e == ESP_OK && !esp_netif_create_default_wifi_sta()) e = ESP_ERR_NO_MEM;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (e == ESP_OK) e = esp_wifi_init(&config);
    if (e == ESP_OK) e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_network, NULL, &wifi_event);
    if (e == ESP_OK) e = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_network, NULL, &ip_event);
    wifi.sta.pmf_cfg.capable = true;
    if (e == ESP_OK) e = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (e == ESP_OK) e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e == ESP_OK) e = esp_wifi_set_ps(WIFI_PS_NONE);
    if (e == ESP_OK && wifi_configured) e = esp_wifi_set_config(WIFI_IF_STA, &wifi);
    memset(&wifi, 0, sizeof(wifi));
    if (e == ESP_OK) e = esp_wifi_start();
    if (e == ESP_OK) network_initialized = true;
    initialization_error = e;
    return e;
}

static esp_err_t ensure_network(const request_t *r)
{
    if (!network_initialized) {
        if (network_attempted) return initialization_error;
        esp_err_t e = initialize_network(true);
        if (e != ESP_OK) return e;
    }
    if (!atomic_load(&got_ip)) {
        if(!wifi_configured)return ESP_ERR_NVS_NOT_FOUND;
        atomic_store(&disconnect_reason,0);
        atomic_store(&reconnect_attempts,0);atomic_store(&reconnect_enabled,true);
        atomic_store(&reconnect_deadline,esp_timer_get_time()+20000000);
        esp_err_t e = esp_wifi_connect();
        if (e != ESP_OK) return e;
        int64_t deadline = esp_timer_get_time() + 20000000;
        while (!atomic_load(&got_ip) && !cancelled((void *)r) && esp_timer_get_time() < deadline){
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (!atomic_load(&got_ip)) {atomic_store(&reconnect_enabled,false);return ESP_ERR_TIMEOUT;}
    }
    if (time(NULL) < 1735689600) {
        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
        }
        int64_t deadline = esp_timer_get_time() + 15000000;
        while (time(NULL) < 1735689600 && !cancelled((void *)r) && esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (time(NULL) < 1735689600) return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void project_row(lizhi_row_t *row, const lizhi_episode_detail_t *detail)
{
    memcpy(row->id, detail->episode_id, sizeof(row->id));
    memcpy(row->title, detail->title, sizeof(row->title));
    row->seconds = detail->duration_seconds;
    row->duration_known = detail->duration_known;
    row->created_known = detail->created_at_known;
    row->created = detail->created_at;
}

static void run_request(const request_t *r, lizhi_job_result_t *out)
{
    out->kind = r->kind; out->generation = r->generation; out->page = r->page;
    if(r->kind==LIZHI_JOB_BOOT){
        esp_err_t e=nvs_flash_init();
        if(e==ESP_OK)e=lizhi_settings_load(&out->settings);
        if(e==ESP_ERR_NVS_NOT_FOUND){
            lizhi_settings_defaults(&out->settings);
            if(CONFIG_LIZHI_ACCOUNT_USER_ID[0]){
                lizhi_account_t legacy={0};snprintf(legacy.id,sizeof(legacy.id),"%s",CONFIG_LIZHI_ACCOUNT_USER_ID);
                snprintf(legacy.name,sizeof(legacy.name),"已绑定账号");
                (void)lizhi_settings_bind(&out->settings,&legacy);
            }
            return;
        }
        if(e!=ESP_OK){out->result.status=LIZHI_CLIENT_SERVICE_ERROR;out->result.transport_error=e;}
        else ESP_LOGI(TAG,"settings restored bound=%d generation=%lu",out->settings.bound,(unsigned long)out->settings.generation);
        return;
    }
    if(r->kind==LIZHI_JOB_SETTINGS){
        out->settings=r->settings;esp_err_t e=lizhi_settings_save(&out->settings);
        if(e!=ESP_OK){out->result.status=LIZHI_CLIENT_SERVICE_ERROR;out->result.transport_error=e;}
        return;
    }
    if(r->kind==LIZHI_JOB_WIFI_SCAN||r->kind==LIZHI_JOB_WIFI_CONNECT){
        bool storage_failed=false;
        esp_err_t e=network_initialized?ESP_OK:network_attempted?initialization_error:initialize_network(false);
        if(e==ESP_OK&&r->kind==LIZHI_JOB_WIFI_SCAN){
            wifi_scan_config_t scan={.show_hidden=false,.scan_type=WIFI_SCAN_TYPE_ACTIVE};
            e=esp_wifi_scan_start(&scan,true);
            if(e==ESP_OK){
                wifi_ap_record_t *raw=calloc(24,sizeof(*raw));
                if(!raw)e=ESP_ERR_NO_MEM;
                else {uint16_t n=24;e=esp_wifi_scan_get_ap_records(&n,raw);
                    if(e==ESP_OK)for(unsigned i=0;i<n&&out->ap_count<12;++i){
                        if(!raw[i].ssid[0])continue;
                        bool dup=false;
                        for(size_t j=0;j<out->ap_count;++j)if(!strncmp(out->aps[j].ssid,(char *)raw[i].ssid,32))dup=true;
                        if(!dup){lizhi_wifi_ap_t *ap=&out->aps[out->ap_count++];memcpy(ap->ssid,raw[i].ssid,32);ap->secure=raw[i].authmode!=WIFI_AUTH_OPEN;}
                    }
                    free(raw);
                }
            }
        }else if(e==ESP_OK){
            if(!r->ssid[0]||!wifi_provisioning_password_is_valid(r->password))e=ESP_ERR_INVALID_ARG;
            else {
                /* Flash旧配置还在不等于驱动仍使用旧网络；失败时也恢复RAM配置。 */
                wifi_config_t previous={0};
                bool had_previous=wifi_configured;
                if(had_previous)e=esp_wifi_get_config(WIFI_IF_STA,&previous);
                if(e!=ESP_OK){memset(&previous,0,sizeof(previous));out->result.status=LIZHI_CLIENT_NETWORK_ERROR;out->result.transport_error=e;return;}
                if(cancelled((void *)r)){memset(&previous,0,sizeof(previous));out->result.status=LIZHI_CLIENT_CANCELLED;return;}
                atomic_store(&reconnect_enabled,false);
                (void)esp_wifi_disconnect();atomic_store(&got_ip,false);vTaskDelay(pdMS_TO_TICKS(150));
                wifi_config_t wifi={0};memcpy(wifi.sta.ssid,r->ssid,strlen(r->ssid));memcpy(wifi.sta.password,r->password,strlen(r->password));wifi.sta.pmf_cfg.capable=true;
                e=esp_wifi_set_config(WIFI_IF_STA,&wifi);wifi_configured=e==ESP_OK;
                if(e==ESP_OK)e=ensure_network(r);
                if(e==ESP_OK&&!cancelled((void *)r)){
                    nvs_handle_t h;e=nvs_open("lizhi_wifi",NVS_READWRITE,&h);
                    if(e==ESP_OK){e=nvs_set_blob(h,"station_v1",&wifi,sizeof(wifi));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}
                    storage_failed=e!=ESP_OK;
                }
                if(!storage_failed&&(e!=ESP_OK||cancelled((void *)r))){
                    atomic_store(&reconnect_enabled,false);
                    (void)esp_wifi_disconnect();atomic_store(&got_ip,false);
                    vTaskDelay(pdMS_TO_TICKS(150));
                    wifi_configured=false;
                    if(had_previous){
                        out->wifi_restore_error=esp_wifi_set_config(WIFI_IF_STA,&previous);
                        if(out->wifi_restore_error==ESP_OK){
                            wifi_configured=true;out->wifi_previous_restored=true;
                            ESP_LOGI(TAG,"Wi-Fi previous configuration restored");
                            /* 下一次业务请求按原网络连接；不把恢复配置误报为联网成功。 */
                        }else ESP_LOGE(TAG,"Wi-Fi config restore failed: %s",esp_err_to_name(out->wifi_restore_error));
                    }
                }
                memset(&previous,0,sizeof(previous));
                memset(&wifi,0,sizeof(wifi));
            }
        }
        if(e!=ESP_OK){out->result.status=storage_failed?LIZHI_CLIENT_SERVICE_ERROR:LIZHI_CLIENT_NETWORK_ERROR;out->result.transport_error=e;}
        return;
    }
    if(r->kind==LIZHI_JOB_FOLLOWS){
        esp_err_t e=lizhi_library_load(r->author_id,r->page,&out->follows,&out->library_total);
        if(e==ESP_OK){out->cached=true;return;}
    }
    esp_err_t e = ensure_network(r);
    if (e != ESP_OK) {
        out->result.status = LIZHI_CLIENT_NETWORK_ERROR;
        out->result.transport_error = e;
        ESP_LOGW(TAG, "network init/connect failed: %s", esp_err_to_name(e));
        return;
    }
    if (cancelled((void *)r)) { out->result.status = LIZHI_CLIENT_CANCELLED; return; }
    if (r->kind == LIZHI_JOB_NETWORK) return;
    if(r->kind==LIZHI_JOB_AUTH_CREATE){out->result=lizhi_auth_create(cancelled,(void *)r,&out->qr);return;}
    if(r->kind==LIZHI_JOB_AUTH_POLL){out->result=lizhi_auth_poll(r->ticket,cancelled,(void *)r,&out->auth_state,&out->account);return;}
    if (r->kind == LIZHI_JOB_FOLLOWS || r->kind==LIZHI_JOB_SYNC) {
        out->result=lizhi_library_sync(r->author_id,cancelled,(void *)r);
        if(out->result.status==LIZHI_CLIENT_OK){
            e=lizhi_library_load(r->author_id,r->page?r->page:1,&out->follows,&out->library_total);
            if(e!=ESP_OK){out->result.status=LIZHI_CLIENT_SERVICE_ERROR;out->result.transport_error=e;}
        }
    } else if (r->kind == LIZHI_JOB_PAGE) {
        out->result = lizhi_load_program_view(r->author_id, r->page, cancelled, (void *)r, out->rows, &out->count);
    } else {
        lizhi_episode_detail_t *detail = calloc(1, sizeof(*detail));
        lizhi_media_probe_t *media = calloc(1, sizeof(*media));
        if (!detail || !media) {
            free(detail); free(media); out->result.status = LIZHI_CLIENT_NO_MEMORY; return;
        }
        out->result = lizhi_client_get_detail(r->episode_id, cancelled, (void *)r, detail);
        if (out->result.status == LIZHI_CLIENT_OK && !detail->has_media_url)
            out->result.status = detail->payment_required_without_media?
                LIZHI_CLIENT_PURCHASE_OR_AUTH_REQUIRED:LIZHI_CLIENT_NO_MEDIA_SOURCE;
        if (out->result.status == LIZHI_CLIENT_OK) {
            project_row(&out->rows[0], detail);
            out->result = lizhi_client_probe_media(detail->media_url, cancelled, (void *)r, media);
            if (out->result.status == LIZHI_CLIENT_OK) memcpy(out->url, media->https_url, sizeof(out->url));
        }
        free(detail); free(media);
    }
}

static void worker(void *unused)
{
    (void)unused;
    request_t r;
    for (;;) {
        xQueueReceive(requests, &r, portMAX_DELAY);
        lizhi_job_result_t *out = NULL;
        while (!cancelled(&r) && xQueueReceive(free_results, &out, pdMS_TO_TICKS(50)) != pdTRUE) { }
        if (!out) { atomic_store(&last_completed,r.generation);memset(&r,0,sizeof(r));continue; }
        memset(out, 0, sizeof(*out));
        run_request(&r, out);
        if (cancelled(&r)) { lizhi_adapter_release(out);atomic_store(&last_completed,r.generation);memset(&r,0,sizeof(r));continue; }
        ESP_LOGI(TAG, "job=%d generation=%lu page=%lu status=%d http=%d bytes=%u stack=%u",
                 r.kind, (unsigned long)r.generation, (unsigned long)r.page,
                 out->result.status, out->result.http_status, (unsigned)out->result.response_bytes,
                 (unsigned)uxTaskGetStackHighWaterMark(NULL));
        while (xQueueSend(results, &out, pdMS_TO_TICKS(50)) != pdTRUE) {
            if (cancelled(&r)) { lizhi_adapter_release(out); break; }
        }
        atomic_store(&last_completed,r.generation);memset(&r,0,sizeof(r));
    }
}

bool lizhi_adapter_init(QueueHandle_t *out)
{
    requests = xQueueCreate(1, sizeof(request_t));
    results = xQueueCreate(2, sizeof(lizhi_job_result_t *));
    free_results = xQueueCreate(2, sizeof(lizhi_job_result_t *));
    if (!requests || !results || !free_results) return false;
    for (size_t i = 0; i < 2; ++i) {
        lizhi_job_result_t *slot = &result_pool[i];
        xQueueSend(free_results, &slot, 0);
    }
    /* 实测最重媒体/TLS路径原8KiB栈余3184；保留约2KiB余量。 */
    if (xTaskCreate(worker, "lizhi_api", 7168, NULL, 4, NULL) != pdPASS) return false;
    *out = results;
    return true;
}

uint32_t lizhi_adapter_submit(lizhi_job_kind_t kind, uint32_t page, const char *episode_id)
{
    return lizhi_adapter_submit_for(kind, page, episode_id, CONFIG_LIZHI_PUBLIC_AUTHOR_ID, "");
}

uint32_t lizhi_adapter_submit_for(lizhi_job_kind_t kind, uint32_t page, const char *episode_id,
                                  const char *author, const char *cursor)
{
    request_t r = {.kind = kind, .page = page};
    if (!author || strlen(author) > LIZHI_ID_BYTES || !cursor || strlen(cursor) > LIZHI_FOLLOW_CURSOR_BYTES) return 0;
    strcpy(r.author_id, author); strcpy(r.cursor, cursor);
    r.generation = atomic_fetch_add(&generation, 1) + 1;
    if (episode_id) {
        if (strlen(episode_id) > LIZHI_ID_BYTES) return 0;
        strcpy(r.episode_id, episode_id);
    }
    if(xQueueOverwrite(requests,&r)!=pdPASS)return 0;
    atomic_store(&last_submitted,r.generation);return r.generation;
}

uint32_t lizhi_adapter_command(lizhi_job_kind_t kind,const lizhi_settings_t *settings,
    const char *ticket,const char *ssid,const char *password)
{
    request_t r={.kind=kind};
    if(ticket&&strlen(ticket)>LIZHI_AUTH_TOKEN_BYTES)return 0;
    if(ssid&&strlen(ssid)>32)return 0;
    if(password&&strlen(password)>64)return 0;
    if(settings)r.settings=*settings;
    if(ticket)strcpy(r.ticket,ticket);
    if(ssid)strcpy(r.ssid,ssid);
    if(password)strcpy(r.password,password);
    r.generation=atomic_fetch_add(&generation,1)+1;
    uint32_t g=xQueueOverwrite(requests,&r)==pdPASS?r.generation:0;
    if(g)atomic_store(&last_submitted,g);
    memset(&r,0,sizeof(r));return g;
}

void lizhi_adapter_cancel(void)
{
    atomic_fetch_add(&generation,1);
    /* 页面退出前等待单worker释放旧HTTP/临时数据；不持有LVGL锁。 */
    while(atomic_load(&last_completed)!=atomic_load(&last_submitted))vTaskDelay(pdMS_TO_TICKS(10));
}
bool lizhi_adapter_online(void) { return atomic_load(&got_ip); }
void lizhi_adapter_release(lizhi_job_result_t *result)
{
    memset(result,0,sizeof(*result));
    if (xQueueSend(free_results, &result, 0) != pdTRUE) ESP_LOGE(TAG, "result slot released twice");
}
