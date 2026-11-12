/* 独立硬件验证入口；定位模式仅保存真实本地进度，不写账号或Wi-Fi。 */
#include "lizhi_client.h"
#include "lizhi_app_logic.h"
#include "lizhi_list_loader.h"
#include "audio_stream.h"
#if CONFIG_LIZHI_PROBE_POSITION
#include "lizhi_progress.h"
#endif
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef CONFIG_LIZHI_PROBE_PLAYBACK
#define CONFIG_LIZHI_PROBE_PLAYBACK 0
#endif

static const char *TAG = "lizhi_probe";
static atomic_bool got_ip;
static esp_netif_t *netif;
static esp_event_handler_instance_t wifi_handler, ip_handler;
static bool wifi_ready, wifi_started;
static volatile uint8_t last_disconnect_reason;
static char configured_ssid[33];

static void on_network(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) atomic_store(&got_ip, true);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        last_disconnect_reason = event ? event->reason : 0;
        atomic_store(&got_ip, false);
        ESP_LOGW(TAG, "Wi-Fi disconnected: reason=%u", (unsigned)last_disconnect_reason);
    }
}

static void scan_summary(const char *target_ssid)
{
    wifi_scan_config_t config = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    if (esp_wifi_scan_start(&config, true) != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed");
        return;
    }
    uint16_t count = 0;
    bool target_found = false;
    if (esp_wifi_scan_get_ap_num(&count) == ESP_OK && count) {
        wifi_ap_record_t *records = calloc(count, sizeof(*records));
        if (records && esp_wifi_scan_get_ap_records(&count, records) == ESP_OK) {
            for (uint16_t i = 0; i < count; ++i)
                if (target_ssid && strncmp((const char *)records[i].ssid, target_ssid, sizeof(records[i].ssid)) == 0)
                    target_found = true;
        }
        free(records);
    }
    ESP_LOGI(TAG, "Wi-Fi scan AP count=%u target_found=%d; credentials remain private", (unsigned)count, target_found);
    (void)esp_wifi_scan_stop();
}

static esp_err_t start_network(void)
{
    esp_err_t error = nvs_flash_init();
    /* 不自动擦除 NVS；版本不匹配或无空间均作为真实故障返回。 */
    if (error != ESP_OK) return error;
    nvs_handle_t store;
    error = nvs_open(CONFIG_LIZHI_PROBE_WIFI_NAMESPACE, NVS_READONLY, &store);
    if (error != ESP_OK) return error;
    char ssid[33] = {0}, password[65] = {0};
    size_t ssid_size = sizeof(ssid), password_size = sizeof(password);
    error = nvs_get_str(store, "ssid", ssid, &ssid_size);
    if (error == ESP_OK) {
        esp_err_t password_error = nvs_get_str(store, "password", password, &password_size);
        if (password_error != ESP_OK && password_error != ESP_ERR_NVS_NOT_FOUND) error = password_error;
    }
    nvs_close(store);
    if (error != ESP_OK || !ssid[0]) return error != ESP_OK ? error : ESP_ERR_INVALID_STATE;
    memcpy(configured_ssid, ssid, sizeof(configured_ssid));
    error = esp_netif_init();
    if (error == ESP_OK) error = esp_event_loop_create_default();
    if (error != ESP_OK) return error;
    netif = esp_netif_create_default_wifi_sta();
    if (!netif) return ESP_ERR_NO_MEM;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    error = esp_wifi_init(&init);
    if (error != ESP_OK) return error;
    wifi_ready = true;
    error = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_network, NULL, &wifi_handler);
    if (error == ESP_OK) error = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_network, NULL, &ip_handler);
    wifi_config_t config = {0};
    memcpy(config.sta.ssid, ssid, strlen(ssid));
    memcpy(config.sta.password, password, strlen(password));
    config.sta.pmf_cfg.capable = true;
    if (error == ESP_OK) error = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (error == ESP_OK) error = esp_wifi_set_mode(WIFI_MODE_STA);
    if (error == ESP_OK) error = esp_wifi_set_ps(WIFI_PS_NONE);
    if (error == ESP_OK) error = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(password, 0, sizeof(password));
    memset(&config, 0, sizeof(config));
    if (error == ESP_OK) error = esp_wifi_start();
    if (error != ESP_OK) return error;
    wifi_started = true;
    error = esp_wifi_connect();
    if (error != ESP_OK) return error;
    int64_t deadline = esp_timer_get_time() + 20000000;
    while (!atomic_load(&got_ip) && esp_timer_get_time() < deadline) vTaskDelay(pdMS_TO_TICKS(100));
    return atomic_load(&got_ip) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t sync_clock(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();
    int64_t deadline = esp_timer_get_time() + 15000000;
    while (time(NULL) < 1735689600 && esp_timer_get_time() < deadline) vTaskDelay(pdMS_TO_TICKS(100));
    return time(NULL) >= 1735689600 ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void stop_network(void)
{
    if (esp_sntp_enabled()) esp_sntp_stop();
    if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler);
    if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
    if (wifi_started) { esp_wifi_disconnect(); esp_wifi_stop(); }
    if (wifi_ready) esp_wifi_deinit();
    if (netif) esp_netif_destroy_default_wifi(netif);
}

static void log_memory(const char *phase)
{
    ESP_LOGI(TAG, "%s: free=%u min_free=%u largest=%u stack_bytes=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static bool log_result(const char *operation, lizhi_client_result_t result)
{
    ESP_LOGI(TAG, "%s: status=%d http=%d transport=%d parse=%d bytes=%u service_known=%d service_code=%" PRId32,
             operation, result.status, result.http_status, result.transport_error, result.parse_error,
             (unsigned)result.response_bytes, result.service_code_known, result.service_code);
    log_memory(operation);
    return result.status == LIZHI_CLIENT_OK;
}

static void probe_follows(void)
{
    lizhi_follow_browser_t *state = calloc(1, sizeof(*state));
    lizhi_follow_view_t *page = calloc(1, sizeof(*page));
    lizhi_row_t *programs = calloc(LIZHI_VIEW_MAX_ITEMS, sizeof(*programs));
    unsigned total = 0, pages = 0, passed = 0, failed = 0;
    bool ended = false, provided_author = false;
    if (!state || !page || !programs) { ++failed; goto done; }
    for (uint32_t target = 1; target <= LIZHI_FOLLOW_MAX_PAGES; ++target) {
        if (!lizhi_follows_begin(state, target, target)) { ++failed; break; }
        lizhi_client_result_t result = lizhi_load_follow_view(CONFIG_LIZHI_PROBE_FOLLOW_OWNER,
            state->cursors[target-1], NULL, NULL, page);
        bool ok = log_result("follow_page", result);
        if (!ok || !lizhi_follows_commit(state, target, page)) { ++failed; break; }
        ++passed; ++pages; total += page->count;
        ESP_LOGI(TAG, "follow_page=%lu count=%u last=%d", (unsigned long)target, (unsigned)page->count, page->last);
        for (size_t i = 0; i < page->count; ++i) {
            provided_author |= !strcmp(page->authors[i].id, CONFIG_LIZHI_PROBE_AUTHOR_ID);
            /* 验证每位真实关注博主可进入节目列表；不输出账号或关注名称。 */
            size_t count = 0;
            result = lizhi_load_program_view(page->authors[i].id, 1, NULL, NULL, programs, &count);
            ok = log_result("follow_programs", result);
            ESP_LOGI(TAG, "program_view_count=%u", (unsigned)count);
            passed += ok; failed += !ok;
        }
        if (page->last) { ended = true; break; }
    }
    if (!ended) ++failed;
done:
    free(state); free(page); free(programs);
    stop_network(); log_memory("follows_finished");
    ESP_LOGI(TAG, "follows_complete pages=%u authors=%u ended=%d provided_author=%d passed=%u failed=%u",
        pages, total, ended, provided_author, passed, failed);
}

static bool probe_playback(const char *url)
{
    audio_stream_set_volume(25);
    esp_err_t error = audio_stream_start(url);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "playback start failed: %s", esp_err_to_name(error));
        return false;
    }
    bool paused = false, resumed = false, failed = false;
    int64_t deadline = esp_timer_get_time() + 60000000;
    uint32_t paused_at = 0;
    while (esp_timer_get_time() < deadline) {
        audio_stream_state_t state = audio_stream_state();
        uint32_t elapsed = audio_stream_elapsed_ms();
        if (state == AUDIO_STREAM_ERROR || !audio_stream_is_active()) { failed = true; break; }
        if (!paused && elapsed >= 10000) {
            error = audio_stream_pause();
            paused_at = audio_stream_elapsed_ms();
            vTaskDelay(pdMS_TO_TICKS(1500));
            uint32_t after = audio_stream_elapsed_ms();
            paused = error == ESP_OK && audio_stream_state() == AUDIO_STREAM_PAUSED &&
                     after >= paused_at && after - paused_at <= 100;
            ESP_LOGI(TAG, "playback pause=%d before=%" PRIu32 " after=%" PRIu32, paused, paused_at, after);
            if (!paused) { failed = true; break; }
            resumed = audio_stream_resume() == ESP_OK;
            if (!resumed) { failed = true; break; }
        }
        if (elapsed >= 30000) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    uint32_t elapsed = audio_stream_elapsed_ms();
    audio_stream_request_stop();
    /* 不能在下载/解码任务尚未结束时停止网络或释放其依赖。 */
    while (audio_stream_is_active()) vTaskDelay(pdMS_TO_TICKS(50));
    bool ok = !failed && paused && resumed && elapsed >= 30000;
    ESP_LOGI(TAG, "playback probe=%d decoded_ms=%" PRIu32 " paused=%d resumed=%d joined=1; audible confirmation required",
             ok, elapsed, paused, resumed);
    log_memory("playback_finished");
    return ok;
}

#if CONFIG_LIZHI_PROBE_POSITION
static void join_audio(void)
{
    audio_stream_request_stop();
    while (audio_stream_is_active()) vTaskDelay(pdMS_TO_TICKS(50));
}

static bool wait_position(uint32_t target)
{
    int64_t deadline = esp_timer_get_time() + 60000000;
    while (esp_timer_get_time() < deadline) {
        if (audio_stream_state() == AUDIO_STREAM_ERROR || !audio_stream_is_active()) return false;
        if (audio_stream_elapsed_ms() >= target) return true;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return false;
}

static bool probe_position(const char *url)
{
    lizhi_progress_t record = {0};
    esp_err_t loaded = lizhi_progress_load(&record, CONFIG_LIZHI_PROBE_AUTHOR_ID);
    bool restoring = loaded == ESP_OK && !strcmp(record.episode.id, CONFIG_LIZHI_PROBE_EPISODE_ID);
    ESP_LOGI(TAG, "position boot restore=%d load=%s saved_ms=%lu", restoring, esp_err_to_name(loaded), (unsigned long)record.position_ms);
    audio_stream_set_volume(25);
    esp_err_t error = restoring ? audio_stream_start_at(url, &record.checkpoint, record.position_ms) : audio_stream_start(url);
    bool ok = error == ESP_OK && wait_position(restoring ? record.position_ms + 2000 : 20000);
    ESP_LOGI(TAG, "position initial/restore=%d actual_ms=%lu", ok, (unsigned long)audio_stream_elapsed_ms());
    if (!ok) { join_audio(); return false; }
    uint32_t before = audio_stream_elapsed_ms(), target = before + 30000;
    audio_checkpoint_t checkpoint;
    ok = audio_stream_checkpoint(target, &checkpoint);
    if (!ok) { ESP_LOGE(TAG, "position checkpoint unavailable"); join_audio(); return false; }
    join_audio();
    error = audio_stream_start_at(url, &checkpoint, target);
    ok = error == ESP_OK && wait_position(target + 1000);
    ESP_LOGI(TAG, "position forward=%d target=%lu actual=%lu", ok, (unsigned long)target, (unsigned long)audio_stream_elapsed_ms());
    if (!ok) { join_audio(); return false; }
    target = audio_stream_elapsed_ms() - 15000;
    ok = audio_stream_checkpoint(target, &checkpoint);
    join_audio();
    if (!ok) { ESP_LOGE(TAG, "position rewind checkpoint unavailable"); return false; }
    error = audio_stream_start_at(url, &checkpoint, target);
    ok = error == ESP_OK && wait_position(target + 1000);
    ESP_LOGI(TAG, "position rewind=%d target=%lu actual=%lu", ok, (unsigned long)target, (unsigned long)audio_stream_elapsed_ms());
    uint32_t saved = audio_stream_elapsed_ms();
    ok = ok && audio_stream_checkpoint(saved, &checkpoint);
    join_audio();
    if (!ok) return false;
    memset(&record, 0, sizeof(record));
    snprintf(record.author, sizeof(record.author), "%s", CONFIG_LIZHI_PROBE_AUTHOR_ID);
    snprintf(record.episode.id, sizeof(record.episode.id), "%s", CONFIG_LIZHI_PROBE_EPISODE_ID);
    /* 用真实详情重新填充标题，避免测试文本进入正式最近记录。 */
    lizhi_episode_detail_t *detail = calloc(1, sizeof(*detail));
    if (!detail) return false;
    lizhi_client_result_t result = lizhi_client_get_detail(record.episode.id, NULL, NULL, detail);
    if (result.status != LIZHI_CLIENT_OK) { free(detail); return false; }
    snprintf(record.episode.title, sizeof(record.episode.title), "%s", detail->title);
    record.episode.duration_known = detail->duration_known;
    record.episode.seconds = detail->duration_seconds;
    free(detail);
    record.position_ms = saved; record.checkpoint = checkpoint;
    error = lizhi_progress_save(&record, CONFIG_LIZHI_PROBE_AUTHOR_ID);
    lizhi_progress_t readback;
    ok = error == ESP_OK && lizhi_progress_load(&readback, CONFIG_LIZHI_PROBE_AUTHOR_ID) == ESP_OK &&
        readback.position_ms == saved && audio_resource_equal(&readback.checkpoint.resource, &checkpoint.resource);
    ESP_LOGI(TAG, "position persistence=%d saved_ms=%lu reboot_restore=%d", ok, (unsigned long)saved, restoring);
    log_memory("position_finished");
    return ok;
}
#endif

void app_main(void)
{
#if CONFIG_LIZHI_PROBE_POSITION
    ESP_LOGI(TAG, "荔枝定位诊断：仅保存真实本地收听进度，不写账号/Wi-Fi，不代表界面验收");
    (void)probe_playback;
#else
    ESP_LOGI(TAG, "荔枝设备探测：有限试听配置=%d，不写账号/进度，不代表正式播放器", CONFIG_LIZHI_PROBE_PLAYBACK);
#endif
    esp_err_t error = start_network();
    if (error == ESP_OK) error = sync_clock();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "网络或校时失败：%s reason=%u；未继续 TLS，不擦除配置", esp_err_to_name(error), (unsigned)last_disconnect_reason);
        if (wifi_started) scan_summary(configured_ssid);
        stop_network();
        return;
    }
    log_memory("network_ready");
    if (CONFIG_LIZHI_PROBE_FOLLOW_OWNER[0]) { probe_follows(); return; }
    lizhi_episode_page_t *page = calloc(1, sizeof(*page));
    lizhi_episode_detail_t *detail = calloc(1, sizeof(*detail));
    lizhi_media_probe_t *media = calloc(1, sizeof(*media));
    if (!page || !detail || !media) {
        ESP_LOGE(TAG, "分配探测结果内存失败");
        free(page); free(detail); free(media); stop_network(); return;
    }
    unsigned passed = 0, failed = 0;
    const uint32_t pages[] = {1, 2, 227, 228};
    for (size_t i = 0; i < sizeof(pages) / sizeof(pages[0]); ++i) {
        lizhi_client_result_t result = lizhi_client_get_page(CONFIG_LIZHI_PROBE_AUTHOR_ID, pages[i], NULL, NULL, page);
        bool ok = log_result("program_page", result);
        passed += ok; failed += !ok;
        ESP_LOGI(TAG, "page=%" PRIu32 " count=%u received=%u duplicates=%u", pages[i],
                 (unsigned)page->count, (unsigned)page->received_count, (unsigned)page->duplicates);
    }
    lizhi_client_result_t result = lizhi_client_get_detail(CONFIG_LIZHI_PROBE_EPISODE_ID, NULL, NULL, detail);
    bool ok = log_result("episode_detail", result);
    passed += ok; failed += !ok;
    if (ok && detail->has_media_url) {
        result = lizhi_client_probe_media(detail->media_url, NULL, NULL, media);
        ok = log_result("media_https_range", result);
        passed += ok; failed += !ok;
        if (ok) ESP_LOGI(TAG, "媒体首段通过，总字节=%" PRIu64 "；未验证整集解码或seek", media->resource_bytes);
    } else {
        ++failed;
        ESP_LOGE(TAG, "媒体探测未执行：详情失败或无播放来源");
    }
    bool can_play = ok && result.status == LIZHI_CLIENT_OK && media->https_url[0];
    free(page); free(detail);
#if CONFIG_LIZHI_PROBE_POSITION
    if (can_play) { bool position_ok = probe_position(media->https_url); passed += position_ok; failed += !position_ok; }
    else ++failed;
#elif CONFIG_LIZHI_PROBE_PLAYBACK
    if (can_play) {
        bool playback_ok = probe_playback(media->https_url);
        passed += playback_ok; failed += !playback_ok;
    } else {
        ++failed;
        ESP_LOGE(TAG, "playback skipped: media probe did not pass");
    }
#else
    (void)can_play;
    (void)probe_playback;
#endif
    free(media);
    stop_network();
    log_memory("finished");
    ESP_LOGI(TAG, "探测通过=%u 失败=%u；仅本轮网络结果，不代表播放/账号/长时验收", passed, failed);
}
