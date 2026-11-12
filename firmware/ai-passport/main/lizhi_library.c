#include "lizhi_library.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
size_t lizhi_library_bytes(size_t n)
{return n<=LIZHI_LIBRARY_MAX_AUTHORS?sizeof(lizhi_library_t)+n*sizeof(lizhi_author_t):0;}
static uint32_t crc(const lizhi_library_t *s)
{
    uint32_t n=UINT32_MAX;const unsigned char *p=(const unsigned char *)s;
    for(size_t i=0;i<lizhi_library_bytes(s->count);++i){
        unsigned char v=i>=offsetof(lizhi_library_t,checksum)&&i<offsetof(lizhi_library_t,checksum)+4?0:p[i];
        n^=v;for(unsigned j=0;j<8;++j)n=(n>>1)^(0xedb88320U & (0U-(n&1U)));
    }return ~n;
}
void lizhi_library_seal(lizhi_library_t *s){s->magic=0x4c5a4c31;s->version=1;s->checksum=crc(s);}
static bool id(const char *s,size_t capacity)
{
    if(!memchr(s,0,capacity)||!s[0]||strlen(s)>19)return false;
    uint64_t n=0;for(;*s;++s){if(*s<'0'||*s>'9'||n>((uint64_t)INT64_MAX-(unsigned)(*s-'0'))/10)return false;n=n*10+(unsigned)(*s-'0');}return n!=0;
}
bool lizhi_library_valid(const lizhi_library_t *s,size_t length,const char *owner)
{
    if(!s||!owner||length<sizeof(*s)||s->count>LIZHI_LIBRARY_MAX_AUTHORS||length!=lizhi_library_bytes(s->count)||
        s->magic!=0x4c5a4c31||s->version!=1||!id(s->owner,sizeof(s->owner))||strcmp(s->owner,owner)||s->checksum!=crc(s))return false;
    for(size_t i=0;i<s->count;++i){
        if(!id(s->authors[i].id,sizeof(s->authors[i].id))||!s->authors[i].name[0]||!memchr(s->authors[i].name,0,sizeof(s->authors[i].name)))return false;
        for(size_t j=0;j<i;++j)if(!strcmp(s->authors[i].id,s->authors[j].id))return false;
    }return true;
}
bool lizhi_library_view(const lizhi_library_t *s,uint32_t page,lizhi_follow_view_t *out)
{
    if(!s||!out||!page||page>LIZHI_FOLLOW_MAX_PAGES)return false;
    memset(out,0,sizeof(*out));size_t first=(page-1)*LIZHI_VIEW_MAX_ITEMS;
    if(first>s->count||(s->count&&first==s->count))return false;
    size_t n=s->count-first;if(n>LIZHI_VIEW_MAX_ITEMS)n=LIZHI_VIEW_MAX_ITEMS;
    out->count=n;out->last=first+n==s->count;
    if(n)memcpy(out->authors,s->authors+first,n*sizeof(lizhi_author_t));
    if(!out->last)snprintf(out->cursor,sizeof(out->cursor),"%u",(unsigned)(first+n));
    return true;
}
#ifdef ESP_PLATFORM
#include "nvs.h"
esp_err_t lizhi_library_load(const char *owner,uint32_t page,lizhi_follow_view_t *out,size_t *total)
{
    if(!owner||!out||!total)return ESP_ERR_INVALID_ARG;
    *total=0;memset(out,0,sizeof(*out));nvs_handle_t h;
    esp_err_t e=nvs_open("lizhi_library",NVS_READONLY,&h);if(e!=ESP_OK)return e;
    size_t n=0;e=nvs_get_blob(h,"snapshot_v1",NULL,&n);
    if(e!=ESP_OK){nvs_close(h);return e;}
    if(n<sizeof(lizhi_library_t)||n>lizhi_library_bytes(LIZHI_LIBRARY_MAX_AUTHORS)){nvs_close(h);return ESP_ERR_INVALID_RESPONSE;}
    lizhi_library_t *s=calloc(1,n);if(!s){nvs_close(h);return ESP_ERR_NO_MEM;}
    e=nvs_get_blob(h,"snapshot_v1",s,&n);nvs_close(h);
    if(e==ESP_OK){
        if(!lizhi_library_valid(s,n,owner))e=ESP_ERR_INVALID_RESPONSE;
        else if(!lizhi_library_view(s,page,out))e=ESP_ERR_INVALID_ARG;
        else *total=s->count;
    }
    free(s);return e;
}
lizhi_client_result_t lizhi_library_sync(const char *owner,lizhi_cancel_fn cancel,void *context)
{
    lizhi_client_result_t result={.status=LIZHI_CLIENT_INVALID_ARGUMENT};
    if(!owner||strlen(owner)>LIZHI_ID_BYTES)return result;
    lizhi_library_t *s=calloc(1,lizhi_library_bytes(LIZHI_LIBRARY_MAX_AUTHORS));
    if(!s){result.status=LIZHI_CLIENT_NO_MEMORY;return result;}
    strcpy(s->owner,owner);char cursor[LIZHI_FOLLOW_CURSOR_BYTES+1]={0};bool ended=false;
    for(unsigned request=0;request<=LIZHI_LIBRARY_MAX_AUTHORS;++request){
        lizhi_follow_page_t page;
        result=lizhi_client_get_follows(owner,cursor,cancel,context,&page);
        if(result.status!=LIZHI_CLIENT_OK)break;
        if(s->count+page.count>LIZHI_LIBRARY_MAX_AUTHORS){result.status=LIZHI_CLIENT_BODY_TOO_LARGE;break;}
        for(size_t i=0;i<page.count;++i){
            for(size_t j=0;j<s->count;++j)if(!strcmp(s->authors[j].id,page.authors[i].id))result.status=LIZHI_CLIENT_FORMAT_ERROR;
            if(result.status!=LIZHI_CLIENT_OK)break;
            s->authors[s->count++]=page.authors[i];
        }
        if(result.status!=LIZHI_CLIENT_OK)break;
        if(page.last){ended=true;break;}
        if(!page.count||!page.cursor[0]||!strcmp(page.cursor,cursor)){result.status=LIZHI_CLIENT_FORMAT_ERROR;break;}
        strcpy(cursor,page.cursor);
    }
    if(result.status==LIZHI_CLIENT_OK&&!ended)result.status=LIZHI_CLIENT_FORMAT_ERROR;
    if(cancel&&cancel(context))result.status=LIZHI_CLIENT_CANCELLED;
    if(result.status==LIZHI_CLIENT_OK){
        lizhi_library_seal(s);
        if(!lizhi_library_valid(s,lizhi_library_bytes(s->count),owner))result.status=LIZHI_CLIENT_FORMAT_ERROR;
        else {
            nvs_handle_t h;esp_err_t e=nvs_open("lizhi_library",NVS_READWRITE,&h);
            if(e==ESP_OK){e=nvs_set_blob(h,"snapshot_v1",s,lizhi_library_bytes(s->count));if(e==ESP_OK)e=nvs_commit(h);nvs_close(h);}
            if(e!=ESP_OK){result.status=LIZHI_CLIENT_SERVICE_ERROR;result.transport_error=e;}
        }
    }
    free(s);return result;
}
#endif
