#include "lizhi_progress.h"
#include <stddef.h>
#include <string.h>

static uint32_t checksum(const lizhi_progress_t *r)
{
    const unsigned char *p = (const unsigned char *)r;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < offsetof(lizhi_progress_t, checksum); ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

static bool valid_id(const char *p, size_t capacity)
{
    if (!p || !p[0] || !memchr(p, 0, capacity)) return false;
    for (; *p; ++p) if (*p < '0' || *p > '9') return false;
    return true;
}

void lizhi_progress_seal(lizhi_progress_t *r)
{
    if (!r) return;
    r->magic = 0x4c5a5031; r->version = 1;
    memset(r->scope, 0, sizeof(r->scope));
    memcpy(r->scope, "local", 6);
    r->checksum = checksum(r);
}

bool lizhi_progress_valid(const lizhi_progress_t *r, const char *author)
{
    if (!r || !author || r->magic != 0x4c5a5031 || r->version != 1 ||
        !memchr(r->scope, 0, sizeof(r->scope)) || strcmp(r->scope, "local") ||
        !valid_id(r->author, sizeof(r->author)) || strcmp(author, r->author) ||
        !valid_id(r->episode.id, sizeof(r->episode.id)) ||
        !r->episode.title[0] || !memchr(r->episode.title, 0, sizeof(r->episode.title)) ||
        !r->position_ms || !audio_checkpoint_valid(&r->checkpoint, r->position_ms)) return false;
    return r->checksum == checksum(r);
}

static uint32_t scoped_checksum(const lizhi_scoped_progress_t *r)
{
    const unsigned char *p=(const unsigned char *)r;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<offsetof(lizhi_scoped_progress_t,checksum);++i){
        crc^=p[i];
        for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}
void lizhi_scoped_progress_seal(lizhi_scoped_progress_t *r)
{
    if(!r)return;
    lizhi_progress_seal(&r->record);
    r->checksum=scoped_checksum(r);
}
bool lizhi_scoped_progress_valid(const lizhi_scoped_progress_t *r,const char *owner)
{
    return r&&owner&&valid_id(r->owner,sizeof(r->owner))&&!strcmp(r->owner,owner)&&
        r->checksum==scoped_checksum(r)&&lizhi_progress_valid(&r->record,r->record.author);
}
#ifdef ESP_PLATFORM
#include "nvs.h"
#include <stdlib.h>
static char progress_owner[LIZHI_ID_BYTES+1];
void lizhi_progress_set_owner(const char *owner)
{
    memset(progress_owner,0,sizeof(progress_owner));
    if(owner&&strlen(owner)<=LIZHI_ID_BYTES)memcpy(progress_owner,owner,strlen(owner));
}

static esp_err_t legacy_load(lizhi_progress_t *r, const char *author)
{
    if (!r) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t error = nvs_open("lizhi_progress", NVS_READONLY, &handle);
    if (error != ESP_OK) return error;
    size_t length = sizeof(*r);
    if(progress_owner[0]){
        lizhi_scoped_progress_t scoped={0};length=sizeof(scoped);
        error=nvs_get_blob(handle,"scoped_v1",&scoped,&length);
        if(error==ESP_OK){
            if(length!=sizeof(scoped)||!memchr(scoped.owner,0,sizeof(scoped.owner)))error=ESP_ERR_INVALID_RESPONSE;
            else if(strcmp(scoped.owner,progress_owner))error=ESP_ERR_NVS_NOT_FOUND;
            else if(!lizhi_scoped_progress_valid(&scoped,progress_owner))error=ESP_ERR_INVALID_RESPONSE;
            else {*r=scoped.record;length=sizeof(*r);}
        }
    }else error = nvs_get_blob(handle, "latest_v1", r, &length);
    nvs_close(handle);
    if (error == ESP_OK && (length != sizeof(*r) || !lizhi_progress_valid(r, author ? author : r->author))) return ESP_ERR_INVALID_RESPONSE;
    return error;
}

static uint32_t history_checksum(const lizhi_history_t *h)
{
    const unsigned char *p=(const unsigned char *)h;
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<offsetof(lizhi_history_t,checksum);++i){
        crc^=p[i];
        for(unsigned j=0;j<8;++j)crc=(crc>>1)^(0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}
esp_err_t lizhi_history_load(lizhi_history_t *h)
{
    if(!h)return ESP_ERR_INVALID_ARG;
    memset(h,0,sizeof(*h));
    nvs_handle_t handle;
    esp_err_t e=nvs_open("lizhi_progress",NVS_READONLY,&handle);
    if(e!=ESP_OK)return e;
    size_t length=sizeof(*h);
    e=nvs_get_blob(handle,progress_owner[0]?"history_acct_v1":"history_local",h,&length);
    nvs_close(handle);
    if(e==ESP_ERR_NVS_NOT_FOUND){
        e=legacy_load(&h->records[0],NULL);
        if(e!=ESP_OK)return e;
        h->count=1;
        memcpy(h->owner,progress_owner,sizeof(h->owner));
        return ESP_OK;
    }
    if(e!=ESP_OK)return e;
    if(length!=sizeof(*h)||h->magic!=0x4c5a4831||h->version!=1||h->count>LIZHI_RECENT_COUNT||
       !memchr(h->owner,0,sizeof(h->owner))||h->checksum!=history_checksum(h))return ESP_ERR_INVALID_RESPONSE;
    if(strcmp(h->owner,progress_owner))return ESP_ERR_NVS_NOT_FOUND;
    for(uint32_t i=0;i<h->count;++i){
        if(!lizhi_progress_valid(&h->records[i],h->records[i].author))return ESP_ERR_INVALID_RESPONSE;
        for(uint32_t j=0;j<i;++j)if(!strcmp(h->records[i].episode.id,h->records[j].episode.id))return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
esp_err_t lizhi_progress_load(lizhi_progress_t *r,const char *author)
{
    if(!r)return ESP_ERR_INVALID_ARG;
    lizhi_history_t *h=malloc(sizeof(*h));
    if(!h)return ESP_ERR_NO_MEM;
    esp_err_t e=lizhi_history_load(h);
    if(e==ESP_OK){
        if(!h->count)e=ESP_ERR_NVS_NOT_FOUND;
        else if(author&&strcmp(author,h->records[0].author))e=ESP_ERR_NVS_NOT_FOUND;
        else *r=h->records[0];
    }
    free(h);return e;
}
esp_err_t lizhi_progress_save(lizhi_progress_t *r, const char *author)
{
    if (!r || !author) return ESP_ERR_INVALID_ARG;
    lizhi_progress_seal(r);
    if (!lizhi_progress_valid(r, author)) return ESP_ERR_INVALID_ARG;
    /* 保存由应用任务串行调用；使用已有栈空间，避免播放中再申请2KiB堆。 */
    lizhi_history_t history={0};lizhi_history_t *h=&history;
    esp_err_t error=lizhi_history_load(h);
    if(error==ESP_ERR_NVS_NOT_FOUND){memset(h,0,sizeof(*h));error=ESP_OK;}
    if(error!=ESP_OK)return error;
    uint32_t index=0;
    while(index<h->count&&strcmp(h->records[index].episode.id,r->episode.id))++index;
    if(index==h->count&&h->count<LIZHI_RECENT_COUNT)++h->count;
    if(index>=LIZHI_RECENT_COUNT)index=LIZHI_RECENT_COUNT-1;
    memmove(&h->records[1],&h->records[0],index*sizeof(*r));h->records[0]=*r;
    h->magic=0x4c5a4831;h->version=1;memcpy(h->owner,progress_owner,sizeof(h->owner));
    h->checksum=history_checksum(h);
    nvs_handle_t handle;
    error = nvs_open("lizhi_progress", NVS_READWRITE, &handle);
    if(error!=ESP_OK)return error;
    error=nvs_set_blob(handle,progress_owner[0]?"history_acct_v1":"history_local",h,sizeof(*h));
    if (error == ESP_OK) error = nvs_commit(handle);
    nvs_close(handle);
    return error;
}
#endif
