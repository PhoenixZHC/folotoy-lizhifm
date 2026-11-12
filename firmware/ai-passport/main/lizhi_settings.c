#include "lizhi_settings.h"
#include <stddef.h>
#include <string.h>

static uint32_t crc(const lizhi_settings_t *s)
{
    const unsigned char *p=(const unsigned char *)s;
    uint32_t n=UINT32_MAX;
    for (size_t i=0;i<offsetof(lizhi_settings_t,checksum);++i) {
        n^=p[i]; for (unsigned bit=0;bit<8;++bit) n=(n>>1)^(0xedb88320U & (0U-(n&1U)));
    }
    return ~n;
}
static bool id_valid(const char *s)
{
    if (!memchr(s,0,LIZHI_ID_BYTES+1) || !s[0] || strlen(s)>19) return false;
    uint64_t n=0;
    for (;*s;++s) {
        if (*s<'0'||*s>'9'||n>((uint64_t)INT64_MAX-(unsigned)(*s-'0'))/10) return false;
        n=n*10+(unsigned)(*s-'0');
    }
    return n!=0;
}
void lizhi_settings_seal(lizhi_settings_t *s)
{
    s->magic=0x4c5a5331; s->version=1; s->checksum=crc(s);
}
void lizhi_settings_defaults(lizhi_settings_t *s)
{
    memset(s,0,sizeof(*s)); s->generation=1; s->volume=25; s->brightness=65; s->timeout_seconds=60;
    lizhi_settings_seal(s);
}
bool lizhi_settings_valid(const lizhi_settings_t *s)
{
    return s && s->magic==0x4c5a5331 && s->version==1 && s->generation && s->volume<=100 &&
        s->brightness>=10 && s->brightness<=100 &&
        (s->timeout_seconds==0||s->timeout_seconds==30||s->timeout_seconds==60||s->timeout_seconds==120) &&
        memchr(s->account.id,0,sizeof(s->account.id)) && memchr(s->account.name,0,sizeof(s->account.name)) &&
        (s->bound ? id_valid(s->account.id) : !s->account.id[0] && !s->account.name[0]) &&
        s->checksum==crc(s);
}
bool lizhi_settings_bind(lizhi_settings_t *s,const lizhi_account_t *account)
{
    if (!s || !account || !id_valid(account->id) || !memchr(account->name,0,sizeof(account->name))) return false;
    s->account=*account; s->bound=true; if (!++s->generation) s->generation=1;
    lizhi_settings_seal(s); return true;
}
void lizhi_settings_unbind(lizhi_settings_t *s)
{
    memset(&s->account,0,sizeof(s->account)); s->bound=false;
    if (!++s->generation) s->generation=1;
    lizhi_settings_seal(s);
}
#ifdef ESP_PLATFORM
#include "nvs.h"
esp_err_t lizhi_settings_load(lizhi_settings_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    lizhi_settings_defaults(s);
    nvs_handle_t handle;
    esp_err_t e=nvs_open("lizhi_settings",NVS_READONLY,&handle);
    if (e!=ESP_OK) return e;
    lizhi_settings_t tmp={0}; size_t size=sizeof(tmp);
    e=nvs_get_blob(handle,"config_v1",&tmp,&size);nvs_close(handle);
    if (e==ESP_OK && (size!=sizeof(tmp)||!lizhi_settings_valid(&tmp))) return ESP_ERR_INVALID_RESPONSE;
    if (e==ESP_OK) *s=tmp;
    return e;
}
esp_err_t lizhi_settings_save(lizhi_settings_t *s)
{
    if (!s) return ESP_ERR_INVALID_ARG;
    lizhi_settings_seal(s);
    if (!lizhi_settings_valid(s)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t e=nvs_open("lizhi_settings",NVS_READWRITE,&handle);
    if (e!=ESP_OK)return e;
    e=nvs_set_blob(handle,"config_v1",s,sizeof(*s));
    if(e==ESP_OK)e=nvs_commit(handle);
    nvs_close(handle);return e;
}
#endif
