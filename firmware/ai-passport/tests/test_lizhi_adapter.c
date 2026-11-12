/* 用内存中的Wi-Fi/NVS替身运行实际adapter；不联网、不读取设备配置。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "adapter_platform.h"
#include "../main/lizhi_direct_adapter.c"

static unsigned checks, failures;
#define CHECK(expr) do { ++checks; if (!(expr)) { ++failures; fprintf(stderr,"line %d: %s\n",__LINE__,#expr); } } while (0)
static wifi_config_t live_wifi, saved_wifi, pending_wifi;
static lizhi_settings_t stored_settings, pending_settings;
static bool settings_write;
static int64_t now_us;
static bool connect_ok, cancel_connect;
static esp_err_t get_error, set_new_error, set_old_error, write_error, commit_error;
static unsigned writes, commits, disconnects, connects;

static void reset(bool old)
{
    memset(&live_wifi,0,sizeof(live_wifi));
    if(old)strcpy((char *)live_wifi.sta.ssid,"old-hotspot");
    saved_wifi=live_wifi;pending_wifi=live_wifi;
    network_initialized=true;network_attempted=true;initialization_error=ESP_OK;wifi_configured=old;
    atomic_store(&generation,1);atomic_store(&got_ip,old);atomic_store(&reconnect_enabled,old);
    atomic_store(&reconnect_deadline,0);atomic_store(&reconnect_attempts,0);
    connect_ok=false;cancel_connect=false;get_error=set_new_error=set_old_error=write_error=commit_error=ESP_OK;
    writes=commits=disconnects=connects=0;now_us=0;
    lizhi_settings_defaults(&stored_settings);pending_settings=stored_settings;settings_write=false;
}
static lizhi_job_result_t run_connect(void)
{
    request_t request={.kind=LIZHI_JOB_WIFI_CONNECT,.generation=1};
    strcpy(request.ssid,"new-hotspot");strcpy(request.password,"valid-test-password");
    lizhi_job_result_t result={0};run_request(&request,&result);return result;
}
esp_err_t esp_wifi_get_config(int unused,wifi_config_t *out){(void)unused;if(get_error)return get_error;*out=live_wifi;return ESP_OK;}
esp_err_t esp_wifi_set_config(int unused,const wifi_config_t *config){
    (void)unused;esp_err_t e=!strcmp((const char *)config->sta.ssid,"old-hotspot")?set_old_error:set_new_error;
    if(e==ESP_OK)live_wifi=*config;
    return e;
}
esp_err_t esp_wifi_disconnect(void){++disconnects;atomic_store(&got_ip,false);return ESP_OK;}
esp_err_t esp_wifi_connect(void){
    ++connects;
    if(cancel_connect)atomic_fetch_add(&generation,1);
    else if(connect_ok||!strcmp((char *)live_wifi.sta.ssid,"old-hotspot"))atomic_store(&got_ip,true);
    return ESP_OK;
}
int64_t esp_timer_get_time(void){return now_us;}
void vTaskDelay(unsigned ms){now_us+=(int64_t)ms*1000;}
esp_err_t nvs_open(const char *ns,int mode,nvs_handle_t *h){(void)ns;(void)mode;*h=1;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n){
    (void)h;++writes;if(write_error)return write_error;
    if(!strcmp(key,"station_v1")){CHECK(n==sizeof(pending_wifi));pending_wifi=*(const wifi_config_t *)p;}
    else {CHECK(n==sizeof(pending_settings));pending_settings=*(const lizhi_settings_t *)p;settings_write=true;}
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h){
    (void)h;++commits;
    if(!commit_error){if(settings_write)stored_settings=pending_settings;else saved_wifi=pending_wifi;}
    return commit_error;
}
void nvs_close(nvs_handle_t h){(void)h;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "test error";}
/* 非本次场景使用的入口仍提供确定的失败值，防止意外走通。 */
esp_err_t nvs_flash_init(void){return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n){(void)h;(void)k;(void)p;(void)n;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_get_str(nvs_handle_t h,const char *k,char *p,size_t *n){(void)h;(void)k;(void)p;(void)n;return ESP_ERR_NVS_NOT_FOUND;}
esp_err_t esp_netif_init(void){return ESP_FAIL;}
esp_err_t esp_event_loop_create_default(void){return ESP_FAIL;}
void *esp_netif_create_default_wifi_sta(void){return NULL;}
esp_err_t esp_wifi_init(const wifi_init_config_t *p){(void)p;return ESP_FAIL;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int32_t i,void (*f)(void *,esp_event_base_t,int32_t,void *),void *p,esp_event_handler_instance_t *o){(void)b;(void)i;(void)f;(void)p;(void)o;return ESP_FAIL;}
esp_err_t esp_wifi_set_storage(int x){(void)x;return ESP_FAIL;}
esp_err_t esp_wifi_set_mode(int x){(void)x;return ESP_FAIL;}
esp_err_t esp_wifi_set_ps(int x){(void)x;return ESP_FAIL;}
esp_err_t esp_wifi_start(void){return ESP_FAIL;}
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *p,bool wait){(void)p;(void)wait;return ESP_FAIL;}
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *p){(void)n;(void)p;return ESP_FAIL;}
bool esp_sntp_enabled(void){return true;}
void esp_sntp_setoperatingmode(int x){(void)x;}
void esp_sntp_setservername(int x,const char *p){(void)x;(void)p;}
void esp_sntp_init(void){}
esp_err_t lizhi_library_load(const char *s,uint32_t p,lizhi_follow_view_t *v,size_t *n){(void)s;(void)p;(void)v;(void)n;return ESP_FAIL;}
lizhi_client_result_t lizhi_library_sync(const char *s,lizhi_cancel_fn f,void *p){(void)s;(void)f;(void)p;return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR};}
lizhi_client_result_t lizhi_auth_create(lizhi_cancel_fn f,void *p,lizhi_auth_qr_t *q){(void)f;(void)p;(void)q;return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR};}
lizhi_client_result_t lizhi_auth_poll(const char *s,lizhi_cancel_fn f,void *p,lizhi_auth_state_t *a,lizhi_account_t *b){(void)s;(void)f;(void)p;(void)a;(void)b;return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR};}
lizhi_client_result_t lizhi_load_program_view(const char *s,uint32_t n,lizhi_cancel_fn f,void *p,lizhi_row_t *r,size_t *c){(void)s;(void)n;(void)f;(void)p;(void)r;(void)c;return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR};}
lizhi_client_result_t lizhi_client_get_detail(const char *s,lizhi_cancel_fn f,void *p,lizhi_episode_detail_t *d){(void)s;(void)f;(void)p;(void)d;return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR};}
lizhi_client_result_t lizhi_client_probe_media(const char *s,lizhi_cancel_fn f,void *p,lizhi_media_probe_t *m){(void)s;(void)f;(void)p;(void)m;return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR};}

int main(void)
{
    reset(true);lizhi_job_result_t r=run_connect();
    CHECK(r.result.status==LIZHI_CLIENT_NETWORK_ERROR);
    CHECK(!strcmp((char *)live_wifi.sta.ssid,"old-hotspot"));
    CHECK(!strcmp((char *)saved_wifi.sta.ssid,"old-hotspot"));CHECK(writes==0);CHECK(wifi_configured);
    reset(false);r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_NETWORK_ERROR);CHECK(!wifi_configured);CHECK(writes==0);
    reset(true);connect_ok=true;r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_OK);
    CHECK(!strcmp((char *)saved_wifi.sta.ssid,"new-hotspot"));CHECK(atomic_load(&got_ip));CHECK(writes==1&&commits==1);
    reset(true);cancel_connect=true;r=run_connect();
    CHECK(!strcmp((char *)live_wifi.sta.ssid,"old-hotspot"));CHECK(writes==0);
    reset(true);get_error=ESP_FAIL;r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_NETWORK_ERROR);CHECK(disconnects==0);CHECK(connects==0);
    reset(true);set_new_error=ESP_FAIL;r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_NETWORK_ERROR);CHECK(wifi_configured);
    CHECK(!strcmp((char *)live_wifi.sta.ssid,"old-hotspot"));CHECK(writes==0);
    reset(true);connect_ok=true;commit_error=ESP_FAIL;r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_SERVICE_ERROR);
    CHECK(!strcmp((char *)live_wifi.sta.ssid,"new-hotspot"));CHECK(!strcmp((char *)saved_wifi.sta.ssid,"old-hotspot"));CHECK(atomic_load(&got_ip));
    reset(true);set_old_error=ESP_FAIL;r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_NETWORK_ERROR);
    CHECK(r.wifi_restore_error==ESP_FAIL);CHECK(!r.wifi_previous_restored);CHECK(!wifi_configured);CHECK(writes==0);
    reset(true);atomic_store(&generation,2);r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_CANCELLED);
    CHECK(disconnects==0);CHECK(writes==0);CHECK(!strcmp((char *)live_wifi.sta.ssid,"old-hotspot"));
    reset(true);connect_ok=true;write_error=ESP_FAIL;r=run_connect();CHECK(r.result.status==LIZHI_CLIENT_SERVICE_ERROR);
    CHECK(!strcmp((char *)live_wifi.sta.ssid,"new-hotspot"));CHECK(!strcmp((char *)saved_wifi.sta.ssid,"old-hotspot"));CHECK(commits==0);

    /* 真正的账号保存分支：换账号/解绑在写入失败时不能覆盖原绑定。 */
    for(unsigned fail=0;fail<3;++fail){
        reset(true);
        lizhi_account_t account={.id="123",.name="first"};CHECK(lizhi_settings_bind(&stored_settings,&account));
        lizhi_settings_t original=stored_settings;
        request_t request={.kind=LIZHI_JOB_SETTINGS,.generation=1,.settings=stored_settings};
        account=(lizhi_account_t){.id="456",.name="second"};CHECK(lizhi_settings_bind(&request.settings,&account));
        if(fail==1)write_error=ESP_FAIL;
        if(fail==2)commit_error=ESP_FAIL;
        memset(&r,0,sizeof(r));run_request(&request,&r);
        CHECK(r.result.status==(fail?LIZHI_CLIENT_SERVICE_ERROR:LIZHI_CLIENT_OK));
        if(fail)CHECK(!memcmp(&stored_settings,&original,sizeof(original)));
        else CHECK(!strcmp(stored_settings.account.id,"456"));
        CHECK(!strcmp((char *)live_wifi.sta.ssid,"old-hotspot"));
        write_error=commit_error=ESP_OK;settings_write=false;
        request.settings=stored_settings;lizhi_settings_unbind(&request.settings);
        memset(&r,0,sizeof(r));run_request(&request,&r);
        CHECK(r.result.status==LIZHI_CLIENT_OK);CHECK(!stored_settings.bound);CHECK(lizhi_settings_valid(&stored_settings));
        CHECK(!strcmp((char *)saved_wifi.sta.ssid,"old-hotspot"));
    }
    printf("adapter recovery: %u passed, %u failed\n",checks-failures,failures);return failures?1:0;
}
