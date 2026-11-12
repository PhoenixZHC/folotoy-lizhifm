#include "lizhi_app.h"
#include "lizhi_direct_adapter.h"
#include "lizhi_progress.h"
#include "wifi_provisioning_logic.h"
#include "audio_stream.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "device_music_ui.h"
#include "display_policy.h"
#include "esp_wifi.h"
#include "nvs.h"
#include <limits.h>
#if !LV_USE_FONT_COMPRESSED
#error "Lizhi UI reuses compressed CJK fonts and requires CONFIG_LV_USE_FONT_COMPRESSED=y"
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if CONFIG_LIZHI_SERIAL_TEST
#include "driver/usb_serial_jtag.h"
static void serial_test_poll(void);
#endif


typedef enum { HOME, LIST, PLAYER, MENU, VOLUME, FOLLOWS, SETTINGS, ACCOUNT, AUTH,
    WIFI, PASSWORD, WIFI_ACTIONS, LOGOUT, BRIGHTNESS, TIMEOUT, ABOUT, RECENTS } screen_t;
typedef struct { bsp_btn_t button; bsp_btn_ev_t event; } lizhi_key_t;

static const char *TAG = "lizhi_app";
static QueueHandle_t keys, results;
static lizhi_browser_t browser;
static screen_t screen;
static bool busy, ready;
static int battery_percent=-1;
static uint32_t active_generation;
static int neighbor_after_page; /* -1最后一条，+1第一条，0仅浏览。 */
static size_t menu_selection;
static lizhi_row_t playing;
static char audio_episode_id[LIZHI_ID_BYTES+1];
static char status_text[160];
static lv_obj_t *ui_root;
static device_music_ui_player_widgets_t player_widgets;
static device_music_ui_volume_widgets_t volume_widgets;
static display_policy_t display;
static screen_t rendered_screen = (screen_t)-1;
static char rendered_status[160];
static uint32_t rendered_page;
static size_t rendered_selection;
static bool rendered_busy;
static audio_stream_state_t rendered_audio = (audio_stream_state_t)-1;
static TickType_t last_click[3];
static lizhi_progress_t recent;
static bool recent_valid, progress_loaded, pending_restore;
static audio_checkpoint_t pending_checkpoint;
static uint32_t pending_position, last_saved_position;
static int64_t last_save_us;
static size_t home_selection;
static size_t recent_selection;
static lizhi_history_t *recent_history;
static char retained_url[512];
static esp_err_t progress_error;
static lizhi_author_t selected_author = {.id = CONFIG_LIZHI_PUBLIC_AUTHOR_ID, .name = CONFIG_LIZHI_PUBLIC_AUTHOR_NAME};
static lizhi_follow_browser_t follows;
static lizhi_settings_t settings;
static size_t settings_selection, account_selection, wifi_selection, wifi_action, timeout_selection, confirm_selection;
static lizhi_wifi_ap_t access_points[12];
static size_t access_point_count, library_total;
static wifi_password_editor_t password_editor;
static lizhi_auth_qr_t auth_qr;
static bool auth_valid, account_changed;
static bool configuration_pending;
static int64_t auth_deadline, auth_next_poll;
static screen_t settings_return=SETTINGS;


static bool account_bound(void) { return settings.bound; }
static void submit_command(lizhi_job_kind_t kind,const lizhi_settings_t *value,
    const char *ticket,const char *ssid,const char *password)
{
    configuration_pending=kind==LIZHI_JOB_SETTINGS||kind==LIZHI_JOB_WIFI_CONNECT;
    active_generation=lizhi_adapter_command(kind,value,ticket,ssid,password);
    busy=active_generation!=0;
    if(!busy){configuration_pending=false;snprintf(status_text,sizeof(status_text),"无法提交后台操作");}
}
static void persist_settings(screen_t destination)
{
    lizhi_settings_t updated=settings;
    updated.volume=(uint8_t)audio_stream_volume();
    settings_return=destination;submit_command(LIZHI_JOB_SETTINGS,&updated,NULL,NULL,NULL);
}

static void key_callback(bsp_btn_t button, bsp_btn_ev_t event, void *unused)
{
    (void)unused;
    if (event != BSP_BTN_CLICK && event != BSP_BTN_LONG) return;
    lizhi_key_t key = {button, event};
    /* 按键定时器回调只派发事件；队列满时单独报警，不能阻塞定时器。 */
    if (xQueueSend(keys, &key, 0) != pdTRUE) ESP_LOGW(TAG, "按键队列已满");
}

static void note(const char *text) { snprintf(status_text, sizeof(status_text), "%s", text); }

static int duration_ms(void)
{
    return playing.duration_known && playing.seconds <= INT_MAX / 1000 ? (int)playing.seconds * 1000 : 0;
}

static void draw_status_folio(void)
{
    const device_music_ui_theme_t *theme = device_music_ui_theme();
    lv_obj_t *label = lv_label_create(ui_root);
    lv_obj_remove_style_all(label);
    lv_obj_set_pos(label, 15, 269); lv_obj_set_size(label, 210, 20);
    lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(label, lv_color_hex(theme->background), 0);
    LV_FONT_DECLARE(netease_ui_font_9);
    lv_obj_set_style_text_font(label, &netease_ui_font_9, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(theme->muted_text), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_label_set_text(label, progress_error != ESP_OK ? "收听记录读写失败" : status_text);
}

static size_t current_selection(void)
{
    switch(screen){
    case MENU:return menu_selection;
    case HOME:return home_selection;
    case RECENTS:return recent_selection;
    case FOLLOWS:return follows.selected;
    case SETTINGS:return settings_selection;
    case ACCOUNT:return account_selection;
    case WIFI:return wifi_selection;
    case WIFI_ACTIONS:return wifi_action;
    case LOGOUT:return confirm_selection;
    case TIMEOUT:return timeout_selection;
    default:return browser.selected;
    }
}

static void render(void)
{
    if (!bsp_lvgl_lock(1000)) { ESP_LOGW(TAG, "UI lock timeout"); return; }
    bool online = lizhi_adapter_online();
    wifi_ap_record_t ap = {0};
    bool signal_valid = online && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    device_music_ui_set_network(online, device_music_ui_signal_level(online, signal_valid, ap.rssi));
    time_t now = time(NULL);
    struct tm tm;
    bool clock_valid = now >= 1735689600 && localtime_r(&now, &tm);
    device_music_ui_set_clock(clock_valid ? tm.tm_hour : 0, clock_valid ? tm.tm_min : 0, clock_valid);
    device_music_ui_update_battery(battery_percent);
    bool night = display_policy_night(clock_valid ? tm.tm_hour : 12, clock_valid, device_music_ui_is_night());
    bool theme_changed = night != device_music_ui_is_night();
    device_music_ui_set_night(night);
    audio_stream_state_t audio = audio_stream_state();
    bool paused = audio == AUDIO_STREAM_PAUSED;
    int elapsed = audio_stream_elapsed_ms() > INT_MAX ? INT_MAX : (int)audio_stream_elapsed_ms();
    int duration = duration_ms();
    bool redraw = rendered_screen != screen || theme_changed || rendered_page != browser.page ||
        rendered_selection != current_selection() ||
        (screen!=AUTH && rendered_busy != busy) || rendered_audio != audio || strcmp(rendered_status, status_text);
    if (!redraw) {
        if (screen == PLAYER) device_music_ui_update_player_progress(&player_widgets,
            device_music_ui_progress_percent(elapsed, duration), elapsed, duration);
        if (screen == VOLUME) device_music_ui_update_volume_overlay(&volume_widgets, audio_stream_volume());
        bsp_lvgl_unlock(); return;
    }
    memset(&player_widgets, 0, sizeof(player_widgets));
    memset(&volume_widgets, 0, sizeof(volume_widgets));
    device_music_screen_t id = screen == PLAYER ? (paused ? DEVICE_MUSIC_SCREEN_PAUSED : DEVICE_MUSIC_SCREEN_PLAYING) :
        screen == LIST ? DEVICE_MUSIC_SCREEN_TRACKS : DEVICE_MUSIC_SCREEN_READY;
    device_music_ui_reset_screen(ui_root, id);
    device_music_ui_set_now_playing(audio_stream_is_active() ? playing.title : "", paused);
    device_music_ui_set_playback_context((unsigned)(browser.page ? (browser.page - 1) * LIZHI_VIEW_MAX_ITEMS + browser.selected + 1 : 0),
                                       selected_author.name, audio_stream_volume());
    device_music_ui_set_buffering(audio == AUDIO_STREAM_BUFFERING);
    device_music_ui_set_playback_status(audio==AUDIO_STREAM_ERROR?"播放失败":
        audio==AUDIO_STREAM_STOPPED?"已停止":audio==AUDIO_STREAM_PAUSED?"已暂停":
        audio==AUDIO_STREAM_BUFFERING?"缓冲中":"正在播放");
    device_music_ui_set_media(NULL, status_text);
    if (busy && screen!=AUTH) {
        device_music_ui_draw_progress(ui_root, DEVICE_MUSIC_SCREEN_LOADING_TRACK,
            screen == HOME ? "正在连接。" : screen == FOLLOWS ? "读取关注。" : screen == LIST ? "读取节目。" : "准备播放。", status_text, 1);
        device_music_ui_set_footer(ui_root, "—", "等待", "—");
    } else if (screen == HOME) {
        device_music_ui_row_t rows[3] = {
            {.title = account_bound() ? "我的关注" : "绑定荔枝账号", .sub = account_bound()?settings.account.name:"使用荔枝 App 扫码", .selected = home_selection == 0, .number = 1}};
        size_t n=2;
        rows[1]=(device_music_ui_row_t){.title="最近播放",.sub="最近四集 · 继续收听",.selected=home_selection==1,.number=2};
        rows[n]=(device_music_ui_row_t){.title="设置",.sub="账号 · 网络 · 屏幕",.selected=home_selection==n,.number=(unsigned)n+1};++n;
        device_music_ui_draw_list(ui_root, DEVICE_MUSIC_SCREEN_PLAYLISTS,
                                 "荔枝 FM。", "", rows, n);
    } else if(screen==RECENTS){
        device_music_ui_row_t rows[LIZHI_RECENT_COUNT]={0};
        char times[LIZHI_RECENT_COUNT][64];
        size_t count=recent_history?recent_history->count:0;
        for(size_t i=0;i<count;++i){
            const lizhi_progress_t *r=&recent_history->records[i];
            snprintf(times[i],sizeof(times[i]),"已听 %lu:%02lu / %lu:%02lu",(unsigned long)(r->position_ms/60000),
                (unsigned long)(r->position_ms/1000%60),(unsigned long)(r->episode.seconds/60),(unsigned long)(r->episode.seconds%60));
            if(!r->episode.duration_known)snprintf(times[i],sizeof(times[i]),"已听 %lu:%02lu",(unsigned long)(r->position_ms/60000),(unsigned long)(r->position_ms/1000%60));
            rows[i]=(device_music_ui_row_t){.title=r->episode.title,.sub=times[i],.number=i+1,.selected=i==recent_selection,.scroll_title=true};
        }
        device_music_ui_draw_list(ui_root,DEVICE_MUSIC_SCREEN_TRACKS,"最近播放",count?"最近四集":"暂无记录",rows,count);
        device_music_ui_set_footer(ui_root,"上一个",count?"继续":"返回","下一个");
        note("长按 OK 返回首页");draw_status_folio();
        device_music_ui_set_footer(ui_root, "上移", "打开", "下移");
        draw_status_folio();
        /* 状态文案使用原UI字库和留白，不复用网易云授权流程。 */
    } else if (screen == FOLLOWS) {
        device_music_ui_row_t rows[LIZHI_VIEW_MAX_ITEMS] = {0};
        for (size_t i = 0; i < follows.page.count; ++i)
            rows[i] = (device_music_ui_row_t){.title = follows.page.authors[i].name, .sub = "博主节目",
                .selected = i == follows.selected, .number = (follows.number-1)*LIZHI_VIEW_MAX_ITEMS+i+1};
        char page[32]; snprintf(page, sizeof(page), "%lu/%u", (unsigned long)(follows.page.count ?
            (follows.number-1)*LIZHI_VIEW_MAX_ITEMS+follows.selected+1 : 0), (unsigned)library_total);
        if (follows.page.count) device_music_ui_draw_list(ui_root, DEVICE_MUSIC_SCREEN_PLAYLISTS,
            "我的关注。", page, rows, follows.page.count);
        else device_music_ui_draw_message(ui_root, DEVICE_MUSIC_SCREEN_PLAYLISTS,
            follows.number ? "暂无关注。" : "读取失败。", status_text);
        device_music_ui_set_footer(ui_root, "上一位", "浏览", "下一位");
        draw_status_folio();
    } else if (screen == LIST) {
        device_music_ui_row_t rows[LIZHI_VIEW_MAX_ITEMS] = {0};
        char meta[LIZHI_VIEW_MAX_ITEMS][80];
        for (size_t i = 0; i < browser.count; ++i) {
            const lizhi_row_t *row = &browser.rows[i];
            char time_text[24] = "时长未知", date[24] = "";
            if (row->duration_known) snprintf(time_text, sizeof(time_text), "%" PRIu32 ":%02" PRIu32, row->seconds / 60, row->seconds % 60);
            if (row->created_known && row->created <= INT64_MAX) {
                time_t seconds = (time_t)row->created; struct tm stamp;
                if (localtime_r(&seconds, &stamp)) strftime(date, sizeof(date), "%Y-%m-%d", &stamp);
            }
            snprintf(meta[i], sizeof(meta[i]), "%s · %s", time_text, date);
            rows[i] = (device_music_ui_row_t){.title = row->title, .sub = meta[i],
                .scroll_title = true,
                .selected = browser.selected == i,
                .playing = audio_stream_is_active() && !strcmp(playing.id, row->id),
                .number = (unsigned)((browser.page - 1) * LIZHI_VIEW_MAX_ITEMS + i + 1)};
        }
        if (!browser.count) device_music_ui_draw_message(ui_root, id, "暂无节目。", status_text);
        else {
            char page[24]; snprintf(page, sizeof(page), "%lu%s", (unsigned long)((browser.page-1)*LIZHI_VIEW_MAX_ITEMS+browser.selected+1),
                browser.reached_end ? "" : "+");
            device_music_ui_draw_list(ui_root, id, selected_author.name, page, rows, browser.count);
        }
        device_music_ui_set_footer(ui_root, "上一个", "播放", "下一个");
        draw_status_folio();
    } else if(screen==SETTINGS){
        const char *items[]={"荔枝账号","刷新我的关注","无线网络","屏幕亮度","自动熄屏","关于"};
        device_music_ui_draw_preferences(ui_root,DEVICE_MUSIC_PREF_SETTINGS,status_text,esp_app_get_description()->version,items,6,settings_selection);
        draw_status_folio();
    } else if(screen==ACCOUNT){
        const char *items[]={account_bound()?"切换账号":"扫码绑定账号","退出账号","返回"};
        device_music_ui_draw_menu(ui_root,"荔枝账号",NULL,items,3,account_selection);
        device_music_ui_set_media(NULL,account_bound()?settings.account.name:"尚未绑定");
        draw_status_folio();
    } else if(screen==AUTH){
        device_music_ui_draw_external_qr(ui_root,"扫码绑定荔枝",status_text,auth_valid?auth_qr.url:NULL);
        device_music_ui_set_footer(ui_root,"—","刷新","—");
    } else if(screen==WIFI){
        const char *names[12];bool secure[12];
        for(size_t i=0;i<access_point_count;++i){names[i]=access_points[i].ssid;secure[i]=access_points[i].secure;}
        device_music_ui_draw_wifi_scan(ui_root,names,secure,access_point_count,wifi_selection,status_text);
    } else if(screen==PASSWORD){
        device_music_ui_draw_wifi_password(ui_root,access_points[wifi_selection].ssid,password_editor.text,
            wifi_password_editor_characters(&password_editor),password_editor.character,password_editor.visible,
            wifi_password_editor_group(&password_editor),status_text);
    } else if(screen==WIFI_ACTIONS){
        const char *items[]={"连接此网络","删除末尾字符","显示 / 隐藏密码","继续输入","返回网络列表"};
        device_music_ui_draw_menu(ui_root,"无线网络",NULL,items,5,wifi_action);
        draw_status_folio();
    } else if(screen==LOGOUT){
        const char *items[]={"取消","确认退出"};
        device_music_ui_draw_preferences(ui_root,DEVICE_MUSIC_PREF_LOGOUT,"退出后隐藏此账号的关注与续听",NULL,items,2,confirm_selection);
    } else if(screen==BRIGHTNESS){device_music_ui_draw_brightness(ui_root,settings.brightness);
    } else if(screen==TIMEOUT){
        const char *items[]={"一直亮屏","30秒","60秒","120秒"};
        device_music_ui_draw_preferences(ui_root,DEVICE_MUSIC_PREF_SCREEN_TIMEOUT,NULL,NULL,items,4,timeout_selection);
    } else if(screen==ABOUT){
        const char *items[]={"返回"};
        device_music_ui_draw_preferences(ui_root,DEVICE_MUSIC_PREF_ABOUT,"荔枝 FM · 设备直连",esp_app_get_description()->version,items,0,0);
    } else if (screen == PLAYER || screen == VOLUME) {
        device_music_ui_draw_podcast_player(ui_root, playing.title, selected_author.name,
            device_music_ui_progress_percent(elapsed, duration), elapsed, duration, status_text, &player_widgets);
        if (!playing.duration_known && player_widgets.duration_label) lv_label_set_text(player_widgets.duration_label, "未知");
        if(audio==AUDIO_STREAM_ERROR)
            device_music_ui_set_footer(ui_root, "上一集", "长按重试", "下一集");
        else device_music_ui_set_footer(ui_root, "上一集", paused ? "继续" : "暂停", "下一集");
        if (screen == VOLUME) {
            device_music_ui_draw_volume_overlay(ui_root, audio_stream_volume(), &volume_widgets);
            device_music_ui_set_footer(ui_root, "加音量", "返回", "减音量");
        }
        if (progress_error != ESP_OK) draw_status_folio();
    } else {
        const char *items[] = {"停止并返回列表", "调节音量", "快退15秒", "快进30秒", "从头播放"};
        device_music_ui_draw_menu(ui_root, "快捷导航。", NULL, items, 5, menu_selection);
    }
    rendered_screen = screen; rendered_page = browser.page;
    rendered_selection = current_selection();
    rendered_busy = busy; rendered_audio = audio;
    snprintf(rendered_status, sizeof(rendered_status), "%s", status_text);
    bsp_lvgl_unlock();
}

static bool create_ui(void)
{
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init() || !bsp_lvgl_lock(1000)) return false;
    ui_root = device_music_ui_create_screen(DEVICE_MUSIC_SCREEN_READY);
    if (ui_root) lv_screen_load(ui_root);
    bsp_lvgl_unlock();
    if (!ui_root) return false;
    bsp_display_backlight(settings.brightness);
    return true;
}

static void log_memory(const char *phase)
{
    ESP_LOGI(TAG, "%s free=%u min=%u largest=%u stack=%u", phase,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void save_progress(void)
{
    uint32_t position = audio_stream_elapsed_ms();
    if (!playing.id[0] || strcmp(playing.id,audio_episode_id) || position < 1500 || position == last_saved_position) return;
    audio_checkpoint_t checkpoint;
    if (!audio_stream_checkpoint(position, &checkpoint)) return;
    lizhi_progress_t record = {0};
    snprintf(record.author, sizeof(record.author), "%s", selected_author.id);
    record.episode = playing; record.position_ms = position; record.checkpoint = checkpoint;
    esp_err_t error = lizhi_progress_save(&record, selected_author.id);
    progress_error = error;
    if (error != ESP_OK) {
        note("收听进度保存失败"); ESP_LOGW(TAG, "progress save failed: %s", esp_err_to_name(error)); return;
    }
    recent = record; recent_valid = true; last_saved_position = position;
    ESP_LOGI(TAG, "progress saved position_ms=%lu anchor_ms=%llu", (unsigned long)position,
             (unsigned long long)(checkpoint.frame.time_us/1000));
}

static void stop_audio(void)
{
    if (!audio_stream_is_active()) return;
    note("正在停止播放…"); render();
    audio_stream_request_stop();
    /* 不持有LVGL锁等待，不在音频任务退出前开始新API/TLS。 */
    while (audio_stream_is_active()) vTaskDelay(pdMS_TO_TICKS(50));
    save_progress();
    log_memory("audio_joined");
}

static void load_page(uint32_t target)
{
    if (!target) { note("已到列表边界"); return; }
    stop_audio();
    active_generation = lizhi_adapter_submit_for(LIZHI_JOB_PAGE, target, NULL, selected_author.id, "");
    if (!active_generation || !lizhi_browser_begin(&browser, target, active_generation)) {
        busy = false; note("无法提交列表请求"); return;
    }
    busy = true; note("正在读取节目…");
}

static void load_follows(uint32_t target)
{
    if (!target || target > LIZHI_FOLLOW_MAX_PAGES) { note("关注分页已到设备上限"); return; }
    stop_audio();
    active_generation = lizhi_adapter_submit_for(LIZHI_JOB_FOLLOWS, target, NULL,
        settings.account.id, target == 1 ? "" : follows.cursors[target-1]);
    busy = lizhi_follows_begin(&follows, target, active_generation);
    if (!busy) lizhi_adapter_cancel();
    note(busy ? "正在同步关注…" : "无法提交关注请求");
}

static void play_row(size_t index)
{
    if (index >= browser.count) return;
    stop_audio();
    playing = browser.rows[index]; browser.selected = index;
    pending_restore = false; last_saved_position = 0;
    lizhi_history_t *history=malloc(sizeof(*history));
    if(!history){note("内存不足，无法读取续听位置");return;}
    esp_err_t history_error=lizhi_history_load(history);
    if(history_error==ESP_OK){
        for(uint32_t i=0;i<history->count;++i){
            const lizhi_progress_t *r=&history->records[i];
            if(!strcmp(r->episode.id,playing.id)&&!strcmp(r->author,selected_author.id)){
                /* 已听完的节目重新播放；中途切回保留位置。 */
                if(!playing.duration_known||r->position_ms/1000+2<playing.seconds){
                    pending_checkpoint=r->checkpoint;pending_position=r->position_ms;pending_restore=true;
                }
                break;
            }
        }
    }
    free(history);
    if(history_error!=ESP_OK&&history_error!=ESP_ERR_NVS_NOT_FOUND){note("续听记录读取失败");return;}
    screen = PLAYER;
    active_generation = lizhi_adapter_submit(LIZHI_JOB_MEDIA, 0, playing.id);
    busy = active_generation != 0;
    note(busy ? "正在验证播放来源…" : "无法提交播放请求");
}

static void describe_error(const lizhi_client_result_t *r)
{
    const char *description = "请求失败";
    switch (r->status) {
    case LIZHI_CLIENT_NETWORK_ERROR: description = "网络失败，请检查热点"; break;
    case LIZHI_CLIENT_NEEDS_AUTH: description = "当前内容需要账号授权"; break;
    case LIZHI_CLIENT_FORBIDDEN: description = "服务拒绝访问"; break;
    case LIZHI_CLIENT_RATE_LIMITED: description = "请求过于频繁，请稍后重试"; break;
    case LIZHI_CLIENT_NO_MEMORY: description = "设备内存不足"; break;
    case LIZHI_CLIENT_BODY_TOO_LARGE: description = "服务响应超过设备上限"; break;
    case LIZHI_CLIENT_FORMAT_ERROR: description = "服务数据格式变化"; break;
    case LIZHI_CLIENT_RANGE_UNSUPPORTED: description = "媒体不支持范围验证"; break;
    case LIZHI_CLIENT_MEDIA_FORMAT_UNSUPPORTED: description = "无可验证的MP3播放来源"; break;
    case LIZHI_CLIENT_NO_MEDIA_SOURCE: description = "荔枝未提供可播放地址"; break;
    case LIZHI_CLIENT_PURCHASE_OR_AUTH_REQUIRED: description = "节目需购买或授权，未获播放地址"; break;
    case LIZHI_CLIENT_CANCELLED: description = "请求已取消"; break;
    default: break;
    }
    snprintf(status_text, sizeof(status_text), "%s\n状态%d / HTTP%d / 传输%d", description, r->status, r->http_status, r->transport_error);
}

static void reload_progress(void)
{
    lizhi_progress_set_owner(account_bound()?settings.account.id:NULL);
    memset(&recent,0,sizeof(recent));recent_valid=false;progress_loaded=true;last_saved_position=0;progress_error=ESP_OK;
    esp_err_t error=lizhi_progress_load(&recent,NULL);
    if(error==ESP_OK)recent_valid=true;
    else if(error!=ESP_ERR_NVS_NOT_FOUND){progress_error=error;note("收听记录读取失败");}
    home_selection=0;
}
static void start_auth(void)
{
    stop_audio();auth_valid=false;memset(&auth_qr,0,sizeof(auth_qr));screen=AUTH;
    submit_command(LIZHI_JOB_AUTH_CREATE,NULL,NULL,NULL,NULL);note("正在获取荔枝二维码");
}
static void scan_wifi(void)
{
    stop_audio();screen=WIFI;
    submit_command(LIZHI_JOB_WIFI_SCAN,NULL,NULL,NULL,NULL);note("正在扫描2.4GHz网络");
}

static void consume_result(lizhi_job_result_t *r)
{
    if (r->generation != active_generation || !busy) return;
    busy = false;configuration_pending=false;
    ready = lizhi_adapter_online();
    if (r->result.status != LIZHI_CLIENT_OK) {
        if (r->kind == LIZHI_JOB_PAGE) lizhi_browser_fail(&browser, r->generation);
        if (r->kind == LIZHI_JOB_FOLLOWS) lizhi_follows_cancel(&follows);
        neighbor_after_page = 0;
        if(r->kind==LIZHI_JOB_AUTH_POLL)auth_next_poll=esp_timer_get_time()+3000000;
        if(r->kind==LIZHI_JOB_AUTH_POLL&&r->result.status==LIZHI_CLIENT_NEEDS_AUTH)auth_valid=false;
        if(r->kind==LIZHI_JOB_SETTINGS)account_changed=false;
        if(r->kind==LIZHI_JOB_WIFI_CONNECT&&r->result.status==LIZHI_CLIENT_SERVICE_ERROR){
            note("网络已连接，但配置保存失败");render();return;
        }
        if(r->kind==LIZHI_JOB_WIFI_CONNECT&&r->wifi_restore_error!=ESP_OK){
            note("连接失败，原网络配置恢复失败；请重新选择网络");render();return;
        }
        if(r->kind==LIZHI_JOB_WIFI_CONNECT&&r->wifi_previous_restored){
            note("连接失败，已恢复原网络设置；长按返回");render();return;
        }
        if(r->kind==LIZHI_JOB_NETWORK && r->result.transport_error==ESP_ERR_NVS_NOT_FOUND){scan_wifi();return;}
        describe_error(&r->result); render(); return;
    }
    if(r->kind==LIZHI_JOB_BOOT){
        settings=r->settings;
        audio_stream_set_volume(settings.volume);display_policy_init(&display,settings.timeout_seconds,esp_timer_get_time());
        bsp_display_backlight(settings.brightness);reload_progress();
        active_generation=lizhi_adapter_submit(LIZHI_JOB_NETWORK,0,NULL);busy=active_generation!=0;
        note("正在连接网络与校时…");render();return;
    }
    if(r->kind==LIZHI_JOB_SETTINGS){
        settings=r->settings;screen=settings_return;
        display_policy_init(&display,settings.timeout_seconds,esp_timer_get_time());
        if(account_changed){
            memset(&follows,0,sizeof(follows));library_total=0;lizhi_browser_init(&browser);memset(&playing,0,sizeof(playing));
            reload_progress();account_changed=false;
        }
        if(screen==PLAYER){
            audio_stream_state_t state=audio_stream_state();
            note(state==AUDIO_STREAM_PLAYING?"播放中":state==AUDIO_STREAM_PAUSED?"已暂停":
                state==AUDIO_STREAM_BUFFERING?"正在缓冲音频":state==AUDIO_STREAM_ERROR?"播放失败，可按确定重试":"已停止");
        }else note("设置已保存");
        render();return;
    }
    if(r->kind==LIZHI_JOB_AUTH_CREATE){
        auth_qr=r->qr;auth_valid=true;auth_deadline=esp_timer_get_time()+(int64_t)auth_qr.valid_seconds*1000000;
        auth_next_poll=esp_timer_get_time()+1000000;rendered_screen=(screen_t)-1;
        note("使用荔枝 App 扫码并确认");render();return;
    }
    if(r->kind==LIZHI_JOB_AUTH_POLL){
        if(r->auth_state==LIZHI_AUTH_EXPIRED){auth_valid=false;note("二维码已过期，按确定刷新");}
        else if(r->auth_state==LIZHI_AUTH_CONFIRMED){
            auth_valid=false;memset(&auth_qr,0,sizeof(auth_qr));
            lizhi_settings_t next=settings;
            if(!lizhi_settings_bind(&next,&r->account)){note("账号响应无效，请重新扫码");render();return;}
            account_changed=true;settings_return=HOME;
            submit_command(LIZHI_JOB_SETTINGS,&next,NULL,NULL,NULL);note("已确认，正在保存账号");
        }else auth_next_poll=esp_timer_get_time()+1000000;
        render();return;
    }
    if(r->kind==LIZHI_JOB_WIFI_SCAN){
        memcpy(access_points,r->aps,sizeof(access_points));access_point_count=r->ap_count;wifi_selection=0;
        note(access_point_count?"选择网络，长按返回":"没有找到网络，按确定重扫");render();return;
    }
    if(r->kind==LIZHI_JOB_WIFI_CONNECT){
        memset(&password_editor,0,sizeof(password_editor));screen=HOME;ready=lizhi_adapter_online();
        note("网络连接成功，配置已保存");render();return;
    }
    if(r->kind==LIZHI_JOB_SYNC){
        library_total=r->library_total;memset(&follows,0,sizeof(follows));
        screen=FOLLOWS;follows.pending=1;follows.generation=r->generation;follows.loading=true;
        if(!lizhi_follows_commit(&follows,r->generation,&r->follows))note("关注数据格式错误");
        else note("关注已刷新并保存");
        render();return;
    }
    if (r->kind == LIZHI_JOB_NETWORK) {
        if (!progress_loaded) {
            progress_loaded = true;
            esp_err_t error = lizhi_progress_load(&recent, NULL);
            recent_valid = error == ESP_OK;
            if (error != ESP_OK && error != ESP_ERR_NVS_NOT_FOUND) {
                progress_error = error;
                note("收听记录读取失败"); ESP_LOGW(TAG, "progress load: %s", esp_err_to_name(error));
                render(); return;
            }
        }
        note("网络就绪，按确定浏览节目");
    } else if (r->kind == LIZHI_JOB_FOLLOWS) {
        if (!lizhi_follows_commit(&follows, r->generation, &r->follows)) {
            note("关注分页异常，保留原列表"); render(); return;
        }
        library_total=r->library_total;
        note(r->cached?"已保存的关注 · 设置中可刷新":"关注已同步并保存");
        ESP_LOGI(TAG, "follows page=%lu count=%u last=%d", (unsigned long)follows.number,
            (unsigned)follows.page.count, follows.page.last);
    } else if (r->kind == LIZHI_JOB_PAGE) {
        uint32_t requested_page = browser.pending_page;
        if (!lizhi_browser_commit(&browser, r->generation, r->rows, r->count)) return;
        note(browser.reached_end ? "已确认没有后续节目" : "节目列表已更新");
        if (neighbor_after_page) {
            int direction = neighbor_after_page; neighbor_after_page = 0;
            if (browser.count && browser.page == requested_page) {
                size_t selected = direction > 0 ? 0 : browser.count - 1;
                if (direction == -2 || direction == 2) browser.selected = selected;
                else play_row(selected);
            } else { screen = LIST; note("已到列表边界"); }
        }
        ESP_LOGI(TAG, "list page=%lu count=%u", (unsigned long)browser.page, (unsigned)browser.count);
    } else {
        playing = r->rows[0];
        if (strlen(r->url) >= sizeof(retained_url)) { note("播放地址超过音频模块上限"); render(); return; }
        memcpy(retained_url, r->url, strlen(r->url) + 1);
        esp_err_t error = pending_restore ? audio_stream_start_at(r->url, &pending_checkpoint, pending_position) : audio_stream_start(r->url);
        pending_restore = false;
        if (error != ESP_OK) snprintf(status_text, sizeof(status_text), "无法启动播放：%s", esp_err_to_name(error));
        else {
            snprintf(audio_episode_id,sizeof(audio_episode_id),"%s",playing.id);
            note("正在缓冲音频"); ESP_LOGI(TAG, "audio_start requested");
        }
    }
    render(); log_memory("result_applied");
}

static void handle_key(lizhi_key_t key)
{
    if (key.event == BSP_BTN_CLICK) {
        TickType_t now = xTaskGetTickCount();
        if (last_click[key.button] && now - last_click[key.button] < pdMS_TO_TICKS(160)) return;
        last_click[key.button] = now;
    }
    if (display_policy_wake(&display, esp_timer_get_time())) {
        bsp_display_backlight(settings.brightness); return; /* 首键只唤醒，不执行动作。 */
    }
    display_policy_activity(&display, esp_timer_get_time());
    if (key.event == BSP_BTN_LONG && key.button != BSP_BTN_OK && screen!=PASSWORD) {
        if(!busy&&(screen==PLAYER||screen==VOLUME)){
            screen=VOLUME;
            audio_stream_adjust_volume(key.button==BSP_BTN_UP?5:-5);
            note("音量已调整");
        }
        return;
    }
    bool back = key.event == BSP_BTN_LONG && key.button == BSP_BTN_OK;
    ESP_LOGI(TAG, "key=%d event=%d screen=%d", key.button, key.event, screen);
    if (busy) {
        if (back) {
            if(configuration_pending){note("正在应用配置，请稍候");return;}
            note("正在取消，请稍候");render();
            lizhi_adapter_cancel(); active_generation = 0; busy = false;
            lizhi_browser_cancel(&browser); lizhi_follows_cancel(&follows); neighbor_after_page = 0;
            bool was_auth=screen==AUTH;
            if(was_auth){auth_valid=false;memset(&auth_qr,0,sizeof(auth_qr));}
            screen = was_auth ? ACCOUNT : screen == PLAYER ? LIST : screen == LIST && account_bound() ? FOLLOWS : HOME; note("已取消请求");
        }
        return;
    }
    if (back) {
        if(screen==RECENTS){free(recent_history);recent_history=NULL;screen=HOME;return;}
        if(screen==PLAYER&&audio_stream_state()==AUDIO_STREAM_ERROR){
            pending_position=audio_stream_elapsed_ms();
            pending_restore=audio_stream_checkpoint(pending_position,&pending_checkpoint);
            active_generation=lizhi_adapter_submit(LIZHI_JOB_MEDIA,0,playing.id);
            busy=active_generation!=0;note("正在重连并恢复收听…");return;
        }
        if(screen==PASSWORD){screen=WIFI_ACTIONS;wifi_action=0;return;}
        if(screen==WIFI_ACTIONS){screen=PASSWORD;return;}
        if(screen==AUTH){auth_valid=false;memset(&auth_qr,0,sizeof(auth_qr));screen=ACCOUNT;return;}
        if(screen==HOME){stop_audio();screen=SETTINGS;settings_selection=0;return;}
        if(screen==SETTINGS){screen=HOME;return;}
        if(screen==ACCOUNT||screen==WIFI||screen==LOGOUT||screen==ABOUT||screen==TIMEOUT){screen=SETTINGS;return;}
        if(screen==BRIGHTNESS){persist_settings(SETTINGS);return;}
        if (screen == LIST) screen = account_bound() ? FOLLOWS : HOME;
        else if (screen == FOLLOWS) screen = HOME;
        else if (screen == PLAYER) { screen = MENU; menu_selection = 0; }
        else if (screen == VOLUME) persist_settings(PLAYER);
        else if (screen == MENU) screen = PLAYER;
        return;
    }
    int direction = key.button == BSP_BTN_UP ? -1 : key.button == BSP_BTN_DOWN ? 1 : 0;
    if (screen == HOME) {
        size_t home_count=3;
        if (direction) { home_selection=(home_selection+(direction>0?1:home_count-1))%home_count;return; }
        if (key.button != BSP_BTN_OK) return;
        if(home_selection==home_count-1){screen=SETTINGS;settings_selection=0;return;}
        if(home_selection==0){
            if(account_bound()){screen=FOLLOWS;load_follows(1);}
            else start_auth();
            return;
        }
        if(home_selection==1){
            stop_audio();
            recent_history=calloc(1,sizeof(*recent_history));
            if(!recent_history){note("内存不足，无法读取最近播放");return;}
            esp_err_t e=lizhi_history_load(recent_history);
            if(e!=ESP_OK){recent_history->count=0;if(e!=ESP_ERR_NVS_NOT_FOUND){progress_error=e;note("最近播放读取失败");}}
            recent_selection=0;screen=RECENTS;
        }
    } else if(screen==RECENTS){
        size_t count=recent_history?recent_history->count:0;
        if(direction){if(count)recent_selection=(recent_selection+(direction>0?1:count-1))%count;return;}
        if(!count){free(recent_history);recent_history=NULL;screen=HOME;return;}
        if(key.button==BSP_BTN_OK){
            recent=recent_history->records[recent_selection];
            free(recent_history);recent_history=NULL;
            stop_audio(); playing = recent.episode; pending_checkpoint = recent.checkpoint;
            if (strcmp(selected_author.id, recent.author)) {
                snprintf(selected_author.id, sizeof(selected_author.id), "%s", recent.author);
                snprintf(selected_author.name, sizeof(selected_author.name), "最近收听");
                lizhi_browser_init(&browser);
            }
            pending_position = recent.position_ms; pending_restore = true; last_saved_position = 0;
            screen = PLAYER;
            active_generation = lizhi_adapter_submit(LIZHI_JOB_MEDIA, 0, playing.id);
            busy = active_generation != 0; note("正在恢复收听位置…");
        }
    } else if(screen==SETTINGS){
        if(direction)settings_selection=(settings_selection+(direction>0?1:5))%6;
        else switch(settings_selection){
        case 0:screen=ACCOUNT;account_selection=0;break;
        case 1:
            if(!account_bound()){start_auth();break;}
            stop_audio();active_generation=lizhi_adapter_submit_for(LIZHI_JOB_SYNC,1,NULL,settings.account.id,"");busy=active_generation!=0;note("正在刷新全部关注");break;
        case 2:scan_wifi();break;
        case 3:screen=BRIGHTNESS;break;
        case 4:screen=TIMEOUT;timeout_selection=settings.timeout_seconds==0?0:settings.timeout_seconds==30?1:settings.timeout_seconds==60?2:3;break;
        default:screen=ABOUT;break;
        }
    } else if(screen==ACCOUNT){
        if(direction)account_selection=(account_selection+(direction>0?1:2))%3;
        else if(account_selection==0)start_auth();
        else if(account_selection==1){stop_audio();screen=LOGOUT;confirm_selection=0;}
        else screen=SETTINGS;
    } else if(screen==AUTH){if(!direction)start_auth();
    } else if(screen==LOGOUT){
        if(direction)confirm_selection=1-confirm_selection;
        else if(!confirm_selection)screen=ACCOUNT;
        else {lizhi_settings_t next=settings;lizhi_settings_unbind(&next);account_changed=true;settings_return=HOME;submit_command(LIZHI_JOB_SETTINGS,&next,NULL,NULL,NULL);note("正在退出账号");}
    } else if(screen==WIFI){
        if(direction&&access_point_count)wifi_selection=(wifi_selection+(direction>0?1:access_point_count-1))%access_point_count;
        else if(!direction){
            if(!access_point_count){scan_wifi();return;}
            wifi_password_editor_init(&password_editor);note("短按确定输入，长按确定连接");
            if(access_points[wifi_selection].secure)screen=PASSWORD;
            else {submit_command(LIZHI_JOB_WIFI_CONNECT,NULL,NULL,access_points[wifi_selection].ssid,"");note("正在连接网络");}
        }
    } else if(screen==PASSWORD){
        if(direction)wifi_password_editor_move(&password_editor,direction,key.event==BSP_BTN_LONG);
        else if(!wifi_password_editor_add(&password_editor))note("密码已满，请长按连接");
        else note("字符已输入，长按确定连接");
        rendered_screen=(screen_t)-1;
    } else if(screen==WIFI_ACTIONS){
        if(direction)wifi_action=(wifi_action+(direction>0?1:4))%5;
        else switch(wifi_action){
        case 0:
            if(!password_editor.text[0]||!wifi_provisioning_password_is_valid(password_editor.text)){note("密码需8至63字符或64位十六进制");screen=PASSWORD;break;}
            submit_command(LIZHI_JOB_WIFI_CONNECT,NULL,NULL,access_points[wifi_selection].ssid,password_editor.text);note("正在连接并保存网络");break;
        case 1:wifi_password_editor_delete(&password_editor);screen=PASSWORD;break;
        case 2:password_editor.visible=!password_editor.visible;screen=PASSWORD;break;
        case 3:screen=PASSWORD;break;
        default:memset(&password_editor,0,sizeof(password_editor));screen=WIFI;break;
        }
    } else if(screen==BRIGHTNESS){
        if(direction){int n=settings.brightness+(direction<0?5:-5);settings.brightness=(uint8_t)(n<10?10:n>100?100:n);bsp_display_backlight(settings.brightness);rendered_screen=(screen_t)-1;}
        else persist_settings(SETTINGS);
    } else if(screen==TIMEOUT){
        if(direction)timeout_selection=(timeout_selection+(direction>0?1:3))%4;
        else {const uint16_t seconds[]={0,30,60,120};settings.timeout_seconds=seconds[timeout_selection];persist_settings(SETTINGS);}
    } else if(screen==ABOUT){if(!direction)screen=SETTINGS;
    } else if (screen == FOLLOWS) {
        if (direction) {
            if (direction < 0 && follows.selected > 0) follows.selected--;
            else if (direction > 0 && follows.selected + 1 < follows.page.count) follows.selected++;
            else if (direction < 0 && follows.number > 1) load_follows(follows.number-1);
            else if (direction > 0 && follows.number && !follows.page.last) load_follows(follows.number+1);
            else note("已到关注列表边界");
        } else if (!follows.number) load_follows(1);
        else if (follows.selected < follows.page.count) {
            stop_audio(); selected_author = follows.page.authors[follows.selected];
            lizhi_browser_init(&browser); screen = LIST; load_page(1);
        }
    } else if (screen == LIST) {
        if (direction) {
            bool edge = direction < 0 ? browser.selected == 0 : browser.selected + 1 >= browser.count;
            uint32_t target = edge ? lizhi_browser_page_target(&browser, direction) : 0;
            if (target) { neighbor_after_page = direction < 0 ? -2 : 2; load_page(target); }
            else if (!edge) lizhi_browser_move(&browser, direction);
            else note("已到列表边界");
        } else if (!browser.valid) load_page(1);
        else if (browser.selected < browser.count) play_row(browser.selected);
    } else if (screen == PLAYER) {
        if (direction) {
            if (!browser.valid || browser.selected >= browser.count || strcmp(browser.rows[browser.selected].id, playing.id)) {
                note("请返回节目列表选择其他节目"); return;
            }
            if (direction < 0 && browser.selected > 0) play_row(browser.selected - 1);
            else if (direction > 0 && browser.selected + 1 < browser.count) play_row(browser.selected + 1);
            else {
                uint32_t target = lizhi_browser_page_target(&browser, direction);
                if (target) { neighbor_after_page = direction; load_page(target); }
                else note("已到列表边界");
            }
        } else {
            audio_stream_state_t state = audio_stream_state();
            if (state == AUDIO_STREAM_PAUSED) {
                if (audio_stream_resume() == ESP_OK) note("继续播放");
            } else if (state == AUDIO_STREAM_PLAYING || state == AUDIO_STREAM_BUFFERING) {
                if (audio_stream_pause() == ESP_OK) note("已暂停");
            } else {
                /* 错误重试保留最后已输出的帧位置；从头播放仅由菜单明确触发。 */
                pending_position=audio_stream_elapsed_ms();
                pending_restore=audio_stream_checkpoint(pending_position,&pending_checkpoint);
                active_generation = lizhi_adapter_submit(LIZHI_JOB_MEDIA, 0, playing.id);
                busy = active_generation != 0; note("正在重新解析播放来源…");
            }
        }
    } else if (screen == MENU) {
        if (direction) menu_selection = (menu_selection + (direction > 0 ? 1 : 4)) % 5;
        else if (menu_selection == 0) { stop_audio(); screen = LIST; note("已停止，可浏览其他节目"); }
        else if (menu_selection == 1) screen = VOLUME;
        else if (menu_selection == 4) {
            stop_audio(); pending_restore = false; last_saved_position = 0; screen = PLAYER;
            active_generation = lizhi_adapter_submit(LIZHI_JOB_MEDIA, 0, playing.id);
            busy = active_generation != 0; note("正在从头播放…");
        } else {
            uint32_t current = audio_stream_elapsed_ms();
            uint32_t target = menu_selection == 2 ? (current > 15000 ? current-15000 : 0) :
                (current > UINT32_MAX-30000 ? UINT32_MAX : current+30000);
            int duration = duration_ms();
            if (duration > 1000 && target >= (uint32_t)duration) target = (uint32_t)duration-1000;
            audio_checkpoint_t checkpoint;
            if (!audio_stream_is_active() || !audio_stream_checkpoint(target, &checkpoint)) {
                note("当前位置没有可验证的跳转帧"); return;
            }
            stop_audio(); screen = PLAYER;
            esp_err_t error = audio_stream_start_at(retained_url, &checkpoint, target);
            note(error == ESP_OK ? "正在定位播放…" : "无法定位播放");
        }
    } else if (screen == VOLUME) {
        if (direction) { audio_stream_adjust_volume(direction < 0 ? 5 : -5); note("音量已调整"); }
        else persist_settings(PLAYER);
    }
    if (audio_stream_state() == AUDIO_STREAM_PAUSED) save_progress();
}

void lizhi_app_run(void)
{
    lizhi_settings_defaults(&settings);
    setenv("TZ", "CST-8", 1); tzset();
    display_policy_init(&display, 60, esp_timer_get_time());
    lizhi_browser_init(&browser);
    keys = xQueueCreate(8, sizeof(lizhi_key_t));
#if CONFIG_LIZHI_SERIAL_TEST
    usb_serial_jtag_driver_config_t serial_config={.rx_buffer_size=256,.tx_buffer_size=256};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&serial_config));
#endif
    if (!keys || !create_ui()) { ESP_LOGE(TAG, "界面初始化失败"); return; }
    note("正在初始化…"); render();
    if (!lizhi_adapter_init(&results) || bsp_button_init(key_callback, NULL) != ESP_OK) {
        note("后台或按键初始化失败，请重启"); render(); return;
    }
    audio_stream_set_volume(settings.volume);
    battery_percent=bsp_battery_soc();
    active_generation = lizhi_adapter_submit(LIZHI_JOB_BOOT, 0, NULL);
    busy = active_generation != 0;
    note("正在连接网络与校时…"); render(); log_memory("ui_ready");
    int64_t next_render = 0, next_memory = 0, next_battery = 0;
    audio_stream_state_t previous_state = AUDIO_STREAM_STOPPED;
    for (;;) {
#if CONFIG_LIZHI_SERIAL_TEST
        serial_test_poll();
#endif
        lizhi_job_result_t *result;
        while (xQueueReceive(results, &result, 0) == pdTRUE) { consume_result(result); lizhi_adapter_release(result); }
        lizhi_key_t key;
        if (xQueueReceive(keys, &key, pdMS_TO_TICKS(100)) == pdTRUE) { handle_key(key); render(); }
        audio_stream_state_t state = audio_stream_state();
        if (state != previous_state) {
            previous_state = state;
            ESP_LOGI(TAG, "audio_state=%d elapsed_ms=%lu", state, (unsigned long)audio_stream_elapsed_ms());
            if (state == AUDIO_STREAM_ERROR) note("播放失败，长按OK重试");
            else if (state == AUDIO_STREAM_PLAYING && !busy) note("播放中");
            else if (state == AUDIO_STREAM_STOPPED && !audio_stream_is_active()) save_progress();
        }
        int64_t now = esp_timer_get_time();
        if(now>=next_battery){next_battery=now+5000000;battery_percent=bsp_battery_soc();}
        if(screen==AUTH&&auth_valid&&!busy){
            if(now>=auth_deadline){auth_valid=false;memset(&auth_qr,0,sizeof(auth_qr));note("二维码已过期，按确定刷新");}
            else if(now>=auth_next_poll)submit_command(LIZHI_JOB_AUTH_POLL,NULL,auth_qr.ticket,NULL,NULL);
        }
        if (state == AUDIO_STREAM_STOPPED && !audio_stream_is_active()) save_progress();
        if(audio_stream_take_completed()){
            save_progress();
            if(!busy&&(screen==PLAYER||screen==MENU||screen==VOLUME)&&browser.valid&&
                browser.selected<browser.count&&!strcmp(browser.rows[browser.selected].id,playing.id)){
                screen=PLAYER;
                if(browser.selected+1<browser.count)play_row(browser.selected+1);
                else {
                    uint32_t target=lizhi_browser_page_target(&browser,1);
                    if(target){neighbor_after_page=1;load_page(target);}
                    else note("本页节目已听完");
                }
            }else note("节目已播放完毕");
        }
        if (now - last_save_us >= 60000000) {
            last_save_us = now;
            if (audio_stream_is_active()) save_progress();
        }
        if (display_policy_tick(&display, now)) bsp_display_backlight(0);
        if (now >= next_render) { next_render = now + 500000; render(); }
        if (now >= next_memory) { next_memory = now + 10000000; log_memory("running"); }
    }
}

#if CONFIG_LIZHI_SERIAL_TEST
/* 测试构建才有：字符派发到同一按键队列，不绕过UI状态机。 */
static void serial_test_poll(void)
{
    static char tail[96];static size_t tail_length;static bool reading_tail;
    char ch;
    if(usb_serial_jtag_read_bytes(&ch,1,0)!=1)return;
    if(reading_tail){
        if(ch=='\n'){
            reading_tail=false;tail[tail_length]=0;
            uint64_t offset,time_us;uint32_t target;
            audio_checkpoint_t cp;
            if(sscanf(tail,"%" SCNu64 ",%" SCNu64 ",%" SCNu32,&offset,&time_us,&target)==3&&
               screen==PLAYER&&!busy&&audio_stream_checkpoint(audio_stream_elapsed_ms(),&cp)){
                cp.frame=(audio_frame_anchor_t){offset,time_us};
                if(audio_checkpoint_valid(&cp,target)){
                    stop_audio();screen=PLAYER;
                    esp_err_t e=audio_stream_start_at(retained_url,&cp,target);
                    ESP_LOGI(TAG,"TEST tail verified_input result=%d target=%lu",e,(unsigned long)target);
                }
            }
        }else if(tail_length+1<sizeof(tail))tail[tail_length++]=ch;
        else reading_tail=false;
        return;
    }
    if(ch=='T'){reading_tail=true;tail_length=0;return;}
    if(ch=='f'&&!busy){
        stop_audio();screen=WIFI;
        submit_command(LIZHI_JOB_WIFI_CONNECT,NULL,NULL,"LIZHI_TEST_UNREACHABLE","test-only-password");
        return;
    }
    if(ch=='r'&&!busy){
        screen=HOME;active_generation=lizhi_adapter_submit(LIZHI_JOB_NETWORK,0,NULL);
        busy=active_generation!=0;return;
    }
    if(ch=='p'&&!busy){
        stop_audio();snprintf(selected_author.id,sizeof(selected_author.id),"%s",CONFIG_LIZHI_PUBLIC_AUTHOR_ID);
        snprintf(selected_author.name,sizeof(selected_author.name),"%s",CONFIG_LIZHI_PUBLIC_AUTHOR_NAME);
        lizhi_browser_init(&browser);screen=LIST;load_page(1);return;
    }
    if(ch=='n'&&!busy){
        wifi_config_t config={0};
        if(esp_wifi_get_config(WIFI_IF_STA,&config)==ESP_OK){
            stop_audio();screen=WIFI;
            submit_command(LIZHI_JOB_WIFI_CONNECT,NULL,NULL,(const char *)config.sta.ssid,(const char *)config.sta.password);
            memset(&config,0,sizeof(config));
        }
        return;
    }
    if(ch=='s'){
        ESP_LOGI(TAG,"TEST screen=%d busy=%d selection=%u page=%lu state=%d elapsed=%lu volume=%d history=%u",
            screen,busy,(unsigned)current_selection(),(unsigned long)browser.page,audio_stream_state(),
            (unsigned long)audio_stream_elapsed_ms(),audio_stream_volume(),recent_history?(unsigned)recent_history->count:0);
        return;
    }
    lizhi_key_t key={.event=BSP_BTN_CLICK};
    switch(ch){
    case 'u':key.button=BSP_BTN_UP;break;
    case 'd':key.button=BSP_BTN_DOWN;break;
    case 'o':key.button=BSP_BTN_OK;break;
    case 'U':key.button=BSP_BTN_UP;key.event=BSP_BTN_LONG;break;
    case 'D':key.button=BSP_BTN_DOWN;key.event=BSP_BTN_LONG;break;
    case 'O':key.button=BSP_BTN_OK;key.event=BSP_BTN_LONG;break;
    default:return;
    }
    (void)xQueueSend(keys,&key,0);
}
#endif
