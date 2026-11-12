#include "lizhi_auth.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const cJSON *unique(const cJSON *object, const char *key)
{
    const cJSON *found = NULL;
    for (const cJSON *p = object ? object->child : NULL; p; p = p->next) {
        if (p->string && !strcmp(p->string, key)) { if (found) return NULL; found = p; }
    }
    return found;
}
static bool utf8_valid(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned c = s[i++], extra, minimum;
        if (c < 128) { if (!c) return false; else continue; }
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; minimum = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; minimum = 0x800; c &= 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; minimum = 0x10000; c &= 7; }
        else return false;
        if (extra > n - i) return false;
        for (unsigned j = 0; j < extra; ++j) {
            if ((s[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (s[i++] & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}

/* 先限制深度/节点，避免授权响应消耗无PSRAM设备的栈和堆。 */
static bool bounded_json(const char *s,size_t n)
{
    unsigned depth=0,tokens=0;
    for(size_t i=0;i<n;++i){
        if(s[i]=='"'){
            bool closed=false;
            while(++i<n){
                if((unsigned char)s[i]<32)return false;
                if(s[i]=='"'){closed=true;break;}
                if(s[i]=='\\'){
                    if(++i>=n)return false;
                    if(s[i]=='u'&&i+4<n&&!memcmp(s+i+1,"0000",4))return false;
                }
            }
            if(!closed)return false;
        }else if(s[i]=='{'||s[i]=='['){if(++depth>24||++tokens>512)return false;}
        else if(s[i]=='}'||s[i]==']'){if(!depth)return false;--depth;}
        else if(s[i]==','||s[i]==':'){if(++tokens>512)return false;}
    }
    return depth==0;
}
static cJSON *parse(const char *body, size_t length)
{
    if (!body || !length || length > LIZHI_RESPONSE_MAX_BYTES || memchr(body, 0, length) ||
        !utf8_valid((const unsigned char *)body,length) || !bounded_json(body,length)) return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(body, length, &end, false);
    if (!j) return NULL;
    while (end < body+length && (*end==' ' || *end=='\r' || *end=='\n' || *end=='\t')) ++end;
    if (end != body+length || !cJSON_IsObject(j)) { cJSON_Delete(j); return NULL; }
    return j;
}
static bool text(const cJSON *j, char *out, size_t capacity)
{
    if (!cJSON_IsString(j) || !j->valuestring[0] || strlen(j->valuestring) >= capacity) return false;
    for (const unsigned char *p=(const unsigned char *)j->valuestring; *p; ++p)
        if (*p < 32 || *p == 127) return false;
    strcpy(out, j->valuestring); return true;
}
lizhi_client_status_t lizhi_auth_parse_qr(const char *body, size_t length, lizhi_auth_qr_t *out)
{
    if (!out) return LIZHI_CLIENT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    cJSON *root = parse(body, length);
    if (!root) return LIZHI_CLIENT_FORMAT_ERROR;
    const cJSON *code=unique(root,"rCode"), *data=unique(root,"data"), *valid=unique(data,"valid");
    lizhi_auth_qr_t qr={0};
    lizhi_client_status_t status=LIZHI_CLIENT_FORMAT_ERROR;
    if (cJSON_IsNumber(code) && code->valuedouble != 0) status=LIZHI_CLIENT_SERVICE_ERROR;
    else if (cJSON_IsNumber(code) && cJSON_IsObject(data) && cJSON_IsNumber(valid) &&
        valid->valuedouble >= 1 && valid->valuedouble <= 300 && floor(valid->valuedouble)==valid->valuedouble &&
        text(unique(data,"tk"),qr.ticket,sizeof(qr.ticket)) && text(unique(data,"url"),qr.url,sizeof(qr.url)) &&
        !strncmp(qr.url,"https://growth.lizhi.fm/static/qrcode-login/index.html?",
            strlen("https://growth.lizhi.fm/static/qrcode-login/index.html?"))) {
        qr.valid_seconds=(uint32_t)valid->valuedouble; *out=qr; status=LIZHI_CLIENT_OK;
    }
    cJSON_Delete(root); return status;
}

lizhi_client_status_t lizhi_auth_parse_account(const char *body, size_t length, lizhi_account_t *out)
{
    if (!out) return LIZHI_CLIENT_INVALID_ARGUMENT;
    memset(out,0,sizeof(*out));
    cJSON *root=parse(body,length);
    if(!root)return LIZHI_CLIENT_FORMAT_ERROR;
    const cJSON *code=unique(root,"rcode"),*data=unique(root,"data");
    lizhi_client_status_t status=LIZHI_CLIENT_FORMAT_ERROR;
    lizhi_account_t account={0};
    if(cJSON_IsNumber(code) && code->valuedouble==403)status=LIZHI_CLIENT_NEEDS_AUTH;
    else if(cJSON_IsNumber(code)&&code->valuedouble!=0)status=LIZHI_CLIENT_SERVICE_ERROR;
    else if(cJSON_IsNumber(code)&&cJSON_IsObject(data)&&
        text(unique(data,"userId"),account.id,sizeof(account.id))) {
        uint64_t n=0;bool valid=true;
        for(const char *p=account.id;*p;++p) {
            if(*p<'0'||*p>'9'||n>((uint64_t)INT64_MAX-(unsigned)(*p-'0'))/10){valid=false;break;}
            n=n*10+(unsigned)(*p-'0');
        }
        if(valid&&n) {
            const cJSON *name=unique(data,"name");
            if(!cJSON_GetObjectItemCaseSensitive(data,"name") || text(name,account.name,sizeof(account.name))) {*out=account;status=LIZHI_CLIENT_OK;}
        }
    }
    cJSON_Delete(root);return status;
}
lizhi_client_status_t lizhi_auth_parse_poll(const char *body, size_t length,
    lizhi_auth_state_t *state, char *token, size_t capacity)
{
    if (!state || !token || !capacity) return LIZHI_CLIENT_INVALID_ARGUMENT;
    *state=LIZHI_AUTH_WAITING; token[0]=0;
    cJSON *root=parse(body,length);
    if (!root) return LIZHI_CLIENT_FORMAT_ERROR;
    const cJSON *code=unique(root,"rCode");
    lizhi_client_status_t status=LIZHI_CLIENT_FORMAT_ERROR;
    if (cJSON_IsNumber(code)) {
        if (code->valuedouble==2) status=LIZHI_CLIENT_OK;
        else if (code->valuedouble==1) { *state=LIZHI_AUTH_EXPIRED; status=LIZHI_CLIENT_OK; }
        else if (code->valuedouble==0 && text(unique(unique(root,"data"),"njLoginToken"),token,capacity)) {
            *state=LIZHI_AUTH_CONFIRMED; status=LIZHI_CLIENT_OK;
        } else if (code->valuedouble!=0) status=LIZHI_CLIENT_SERVICE_ERROR;
    }
    cJSON_Delete(root); return status;
}

lizhi_client_status_t lizhi_auth_parse_login(const char *body,size_t length)
{
    cJSON *root=parse(body,length);
    if(!root)return LIZHI_CLIENT_FORMAT_ERROR;
    const cJSON *code=unique(root,"code");
    lizhi_client_status_t status=!cJSON_IsNumber(code)?LIZHI_CLIENT_FORMAT_ERROR:
        code->valuedouble==0?LIZHI_CLIENT_OK:LIZHI_CLIENT_NEEDS_AUTH;
    cJSON_Delete(root);return status;
}
