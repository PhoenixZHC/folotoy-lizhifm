/* 仅主机编译：隔离的内存NVS与协议夹具，不链接到设备入口。 */
#include "lizhi_library.h"
#include "lizhi_progress.h"
#include "lizhi_settings.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {unsigned char data[32000],pending[32000];size_t length,pending_length;} slots[6];
static bool fail_write,fail_commit,cancelled,duplicate,network_fail;
static unsigned follow_calls,checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h)
{
    (void)mode;*h=!strcmp(name,"lizhi_settings")?0:!strcmp(name,"lizhi_library")?1:2;
    return ESP_OK;
}
static unsigned index_for(nvs_handle_t h,const char *key){
    if(h==2){if(!strcmp(key,"latest_v1"))return 3;if(!strcmp(key,"history_acct_v1"))return 4;if(!strcmp(key,"history_local"))return 5;}
    return h;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *length)
{
    unsigned i=index_for(h,key);
    if(!slots[i].length)return ESP_ERR_NVS_NOT_FOUND;
    if(!out){*length=slots[i].length;return ESP_OK;}
    if(*length<slots[i].length)return ESP_ERR_INVALID_SIZE;
    *length=slots[i].length;memcpy(out,slots[i].data,*length);return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *value,size_t length)
{
    if(fail_write)return ESP_FAIL;
    unsigned i=index_for(h,key);assert(length<=sizeof(slots[i].pending));
    memcpy(slots[i].pending,value,length);slots[i].pending_length=length;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    if(fail_commit)return ESP_FAIL;
    for(unsigned i=0;i<6;++i)if(i==h||(h==2&&i>=3)){
        if(slots[i].pending_length){slots[i].length=slots[i].pending_length;memcpy(slots[i].data,slots[i].pending,slots[i].length);slots[i].pending_length=0;}
    }
    return ESP_OK;
}
void nvs_close(nvs_handle_t h){for(unsigned i=0;i<6;++i)if(i==h||(h==2&&i>=3))slots[i].pending_length=0;}
static bool cancel(void *p){(void)p;return cancelled;}
lizhi_client_result_t lizhi_client_get_follows(const char *owner,const char *cursor,
    lizhi_cancel_fn cb,void *context,lizhi_follow_page_t *out)
{
    (void)owner;(void)cursor;(void)cb;(void)context;++follow_calls;
    lizhi_client_result_t r={0};memset(out,0,sizeof(*out));
    if(follow_calls==2&&network_fail){r.status=LIZHI_CLIENT_NETWORK_ERROR;return r;}
    out->count=2;out->last=follow_calls==3;
    snprintf(out->cursor,sizeof(out->cursor),"%u",follow_calls);
    for(unsigned i=0;i<2;++i){
        snprintf(out->authors[i].id,sizeof(out->authors[i].id),"%u",duplicate?100:100+follow_calls*2+i);
        strcpy(out->authors[i].name,"隔离测试博主");
    }
    return r;
}
static void reset_sync(void){follow_calls=0;cancelled=duplicate=network_fail=fail_write=fail_commit=false;}
int main(void)
{
    lizhi_settings_t settings,loaded;
    CHECK(lizhi_settings_load(&settings)==ESP_ERR_NVS_NOT_FOUND);
    CHECK(lizhi_settings_valid(&settings)&&!settings.bound);
    lizhi_account_t account={.id="123",.name="隔离测试账号"};
    CHECK(lizhi_settings_bind(&settings,&account));
    CHECK(lizhi_settings_save(&settings)==ESP_OK);
    CHECK(lizhi_settings_load(&loaded)==ESP_OK&&!strcmp(loaded.account.id,"123"));
    fail_write=true;lizhi_settings_unbind(&settings);
    CHECK(lizhi_settings_save(&settings)!=ESP_OK);
    CHECK(lizhi_settings_load(&loaded)==ESP_OK&&loaded.bound);fail_write=false;
    lizhi_follow_view_t view;size_t count;
    CHECK(lizhi_library_load("123",1,&view,&count)==ESP_ERR_NVS_NOT_FOUND);
    reset_sync();CHECK(lizhi_library_sync("123",cancel,NULL).status==LIZHI_CLIENT_OK&&follow_calls==3);
    CHECK(lizhi_library_load("123",1,&view,&count)==ESP_OK&&count==6&&view.count==4&&!view.last);
    CHECK(lizhi_library_load("123",2,&view,&count)==ESP_OK&&view.count==2&&view.last);
    CHECK(lizhi_library_load("124",1,&view,&count)!=ESP_OK&&count==0);
    reset_sync();network_fail=true;CHECK(lizhi_library_sync("123",cancel,NULL).status==LIZHI_CLIENT_NETWORK_ERROR);
    CHECK(lizhi_library_load("123",2,&view,&count)==ESP_OK&&count==6);
    reset_sync();duplicate=true;CHECK(lizhi_library_sync("123",cancel,NULL).status==LIZHI_CLIENT_FORMAT_ERROR);
    reset_sync();cancelled=true;CHECK(lizhi_library_sync("123",cancel,NULL).status==LIZHI_CLIENT_CANCELLED);
    reset_sync();fail_write=true;CHECK(lizhi_library_sync("124",cancel,NULL).status==LIZHI_CLIENT_SERVICE_ERROR);
    CHECK(lizhi_library_load("123",1,&view,&count)==ESP_OK&&count==6);
    reset_sync();fail_commit=true;CHECK(lizhi_library_sync("124",cancel,NULL).status==LIZHI_CLIENT_SERVICE_ERROR);
    CHECK(lizhi_library_load("123",1,&view,&count)==ESP_OK&&count==6);reset_sync();
    lizhi_progress_t r={.author="456",.episode={.id="789",.title="测试节目"},.position_ms=60000,
        .checkpoint={.resource={.total_bytes=40964473,.validator="\"test-only\"",.is_etag=true},.frame={640000,40000000}}};
    lizhi_progress_set_owner(NULL);CHECK(lizhi_progress_save(&r,r.author)==ESP_OK);
    lizhi_progress_set_owner("123");CHECK(lizhi_progress_load(&r,NULL)==ESP_ERR_NVS_NOT_FOUND);
    CHECK(lizhi_progress_save(&r,r.author)==ESP_OK);
    lizhi_progress_set_owner("124");CHECK(lizhi_progress_load(&r,NULL)==ESP_ERR_NVS_NOT_FOUND);
    lizhi_progress_set_owner("123");CHECK(lizhi_progress_load(&r,NULL)==ESP_OK);
    lizhi_history_t history;
    for(unsigned i=0;i<5;++i){snprintf(r.episode.id,sizeof(r.episode.id),"%u",800+i);CHECK(lizhi_progress_save(&r,r.author)==ESP_OK);}
    CHECK(lizhi_history_load(&history)==ESP_OK&&history.count==4&&!strcmp(history.records[0].episode.id,"804")&&!strcmp(history.records[3].episode.id,"801"));
    strcpy(r.episode.id,"802");r.position_ms=61000;
    CHECK(lizhi_progress_save(&r,r.author)==ESP_OK);
    CHECK(lizhi_history_load(&history)==ESP_OK&&history.count==4&&!strcmp(history.records[0].episode.id,"802")&&history.records[0].position_ms==61000&&!strcmp(history.records[1].episode.id,"804"));
    fail_commit=true;strcpy(r.episode.id,"900");CHECK(lizhi_progress_save(&r,r.author)!=ESP_OK);fail_commit=false;
    CHECK(lizhi_history_load(&history)==ESP_OK&&!strcmp(history.records[0].episode.id,"802"));
    size_t full_length=slots[4].length;
    for(size_t n=1;n<full_length;++n){slots[4].length=n;CHECK(lizhi_history_load(&history)!=ESP_OK);}
    slots[4].length=full_length;
    slots[4].data[1]^=1;CHECK(lizhi_progress_load(&r,NULL)==ESP_ERR_INVALID_RESPONSE);
    lizhi_progress_set_owner(NULL);CHECK(lizhi_progress_load(&r,NULL)==ESP_OK);
    /* 旧单条记录无写入迁移即可读取；下次保存升级为四条格式。 */
    slots[5].length=0;lizhi_progress_seal(&r);memcpy(slots[3].data,&r,sizeof(r));slots[3].length=sizeof(r);
    CHECK(lizhi_history_load(&history)==ESP_OK&&history.count==1);
    CHECK(lizhi_progress_save(&r,r.author)==ESP_OK&&slots[5].length==sizeof(history));
    lizhi_scoped_progress_t old_scoped={.owner="123",.record=r};lizhi_scoped_progress_seal(&old_scoped);
    memcpy(slots[2].data,&old_scoped,sizeof(old_scoped));slots[2].length=sizeof(old_scoped);slots[4].length=0;
    lizhi_progress_set_owner("123");
    CHECK(lizhi_history_load(&history)==ESP_OK&&history.count==1&&!strcmp(history.records[0].episode.id,r.episode.id));
    CHECK(lizhi_progress_save(&r,r.author)==ESP_OK&&slots[4].length==sizeof(history));
    fail_write=true;strcpy(r.episode.id,"999");CHECK(lizhi_progress_save(&r,r.author)!=ESP_OK);fail_write=false;
    CHECK(lizhi_history_load(&history)==ESP_OK&&strcmp(history.records[0].episode.id,"999"));
    /* 按实际界面顺序串起账号保存、重新加载身份和历史作用域，覆盖退出失败。 */
    CHECK(lizhi_settings_load(&settings)==ESP_OK&&settings.bound&&!strcmp(settings.account.id,"123"));
    lizhi_history_t original_history=history;
    for(unsigned unbind=0;unbind<2;++unbind){
        for(unsigned commit=0;commit<2;++commit){
            lizhi_settings_t next=settings;
            if(unbind)lizhi_settings_unbind(&next);
            else {lizhi_account_t other={.id="124",.name="另一个测试账号"};CHECK(lizhi_settings_bind(&next,&other));}
            fail_write=!commit;fail_commit=commit;
            CHECK(lizhi_settings_save(&next)!=ESP_OK);
            fail_write=fail_commit=false;
            CHECK(lizhi_settings_load(&loaded)==ESP_OK);
            CHECK(!memcmp(&loaded,&settings,sizeof(settings)));
            lizhi_progress_set_owner(loaded.account.id);
            CHECK(lizhi_history_load(&history)==ESP_OK);
            CHECK(!memcmp(&history,&original_history,sizeof(history)));
        }
    }
    /* 成功退出只改变身份；本地历史与原账号历史保持各自作用域。 */
    loaded=settings;lizhi_settings_unbind(&loaded);CHECK(lizhi_settings_save(&loaded)==ESP_OK);
    CHECK(lizhi_settings_load(&loaded)==ESP_OK&&!loaded.bound);
    lizhi_progress_set_owner(NULL);CHECK(lizhi_history_load(&history)==ESP_OK&&history.count==1);
    CHECK(!history.owner[0]);
    CHECK(lizhi_settings_save(&settings)==ESP_OK);
    CHECK(lizhi_settings_load(&loaded)==ESP_OK&&loaded.bound);
    lizhi_progress_set_owner(loaded.account.id);CHECK(lizhi_history_load(&history)==ESP_OK);
    CHECK(!memcmp(&history,&original_history,sizeof(history)));
    lizhi_account_t other={.id="124",.name="另一个测试账号"};CHECK(lizhi_settings_bind(&loaded,&other));
    CHECK(lizhi_settings_save(&loaded)==ESP_OK);
    CHECK(lizhi_settings_load(&loaded)==ESP_OK&&!strcmp(loaded.account.id,"124"));
    lizhi_progress_set_owner(loaded.account.id);CHECK(lizhi_history_load(&history)==ESP_ERR_NVS_NOT_FOUND);
    CHECK(lizhi_settings_save(&settings)==ESP_OK);
    lizhi_progress_set_owner(settings.account.id);CHECK(lizhi_history_load(&history)==ESP_OK);
    CHECK(!memcmp(&history,&original_history,sizeof(history)));
    printf("持久化故障与账号隔离：%u通过/0失败\n",checks);
    return 0;
}
