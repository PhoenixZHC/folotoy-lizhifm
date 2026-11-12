#include "lizhi_auth.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>

/* 授权Cookie只在单次确认流程RAM中使用，取得ID后清零，不写Flash。 */
typedef struct {
    char *body;
    size_t length;
    char cookies[2048];
    bool json;
    lizhi_client_status_t status;
    lizhi_cancel_fn cancel;
    void *context;
} auth_response_t;
static bool store_cookie(auth_response_t *r,const char *value)
{
    size_t length=strcspn(value,";"),old=strlen(r->cookies);
    const char *equal=memchr(value,'=',length);
    if(!equal||equal==value)return false;
    size_t name=(size_t)(equal-value);
    for(size_t i=0;i<length;++i)if((unsigned char)value[i]<32||(unsigned char)value[i]>=127)return false;
    /* 同名Cookie替换旧值，避免服务端读到旧会话；不解析或持久化凭证内容。 */
    size_t offset=0;
    while(offset<old){
        size_t end=offset;
        while(end<old&&r->cookies[end]!=';')++end;
        if(end-offset>name&&r->cookies[offset+name]=='='&&!memcmp(r->cookies+offset,value,name)){
            size_t next=end<old?end+2:end;
            memmove(r->cookies+offset,r->cookies+next,old-next+1);
            old-=next-offset;
            if(offset==old&&offset>=2){old-=2;r->cookies[old]=0;}
            break;
        }
        offset=end<old?end+2:end;
    }
    if(old+length+3>=sizeof(r->cookies))return false;
    if(old){memcpy(r->cookies+old,"; ",2);old+=2;}
    memcpy(r->cookies+old,value,length);r->cookies[old+length]=0;return true;
}
static esp_err_t event(esp_http_client_event_t *e)
{
    auth_response_t *r=e->user_data;
    if(r->cancel&&r->cancel(r->context))r->status=LIZHI_CLIENT_CANCELLED;
    if(r->status!=LIZHI_CLIENT_OK)return ESP_FAIL;
    if(e->event_id==HTTP_EVENT_ON_HEADER&&e->header_key&&e->header_value) {
        if(!strcasecmp(e->header_key,"Content-Encoding")&&strcasecmp(e->header_value,"identity"))r->status=LIZHI_CLIENT_UNSUPPORTED_ENCODING;
        if(!strcasecmp(e->header_key,"Content-Type"))r->json=!strncasecmp(e->header_value,"application/json",16)&&
            (!e->header_value[16]||e->header_value[16]==';'||e->header_value[16]==' ');
        if(!strcasecmp(e->header_key,"Set-Cookie")) {
            if(!store_cookie(r,e->header_value))r->status=LIZHI_CLIENT_FORMAT_ERROR;
        }
    } else if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0) {
        if(!e->data||(size_t)e->data_len>LIZHI_RESPONSE_MAX_BYTES-r->length)r->status=LIZHI_CLIENT_BODY_TOO_LARGE;
        else {memcpy(r->body+r->length,e->data,e->data_len);r->length+=e->data_len;r->body[r->length]=0;}
    }
    return r->status==LIZHI_CLIENT_OK?ESP_OK:ESP_FAIL;
}
static lizhi_client_result_t request(const char *url,const char *post,auth_response_t *r)
{
    lizhi_client_result_t result={0};
    r->length=0;r->json=false;r->status=LIZHI_CLIENT_OK;
    memset(r->body,0,LIZHI_RESPONSE_MAX_BYTES+1);
    if(r->cancel&&r->cancel(r->context)){result.status=LIZHI_CLIENT_CANCELLED;return result;}
    esp_http_client_config_t config={.url=url,.method=post?HTTP_METHOD_POST:HTTP_METHOD_GET,
        .timeout_ms=15000,.buffer_size=2048,.buffer_size_tx=4096,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=event,.user_data=r,
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER && CONFIG_ESP_TLS_DYN_BUF_STRATEGY_SUPPORTED
        .tls_dyn_buf_strategy=HTTP_TLS_DYN_BUF_RX_STATIC,
#endif
    };
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client){result.status=LIZHI_CLIENT_NO_MEMORY;return result;}
    esp_err_t err=esp_http_client_set_header(client,"Accept","application/json");
    if(err==ESP_OK)err=esp_http_client_set_header(client,"Accept-Encoding","identity");
    if(err==ESP_OK&&r->cookies[0])err=esp_http_client_set_header(client,"Cookie",r->cookies);
    if(err==ESP_OK&&post)err=esp_http_client_set_header(client,"Content-Type","application/x-www-form-urlencoded");
    if(err==ESP_OK&&post)err=esp_http_client_set_post_field(client,post,strlen(post));
    if(err==ESP_OK)err=esp_http_client_perform(client);
    result.transport_error=err;result.http_status=esp_http_client_get_status_code(client);result.response_bytes=r->length;
    int64_t declared=esp_http_client_get_content_length(client);
    if(r->cancel&&r->cancel(r->context))result.status=LIZHI_CLIENT_CANCELLED;
    else if(r->status!=LIZHI_CLIENT_OK)result.status=r->status;
    else if(result.http_status==401)result.status=LIZHI_CLIENT_NEEDS_AUTH;
    else if(result.http_status==403)result.status=LIZHI_CLIENT_FORBIDDEN;
    else if(result.http_status==429)result.status=LIZHI_CLIENT_RATE_LIMITED;
    else if(result.http_status>=300&&result.http_status<400)result.status=LIZHI_CLIENT_REDIRECT_REJECTED;
    else if(err!=ESP_OK)result.status=LIZHI_CLIENT_NETWORK_ERROR;
    else if(result.http_status!=200)result.status=LIZHI_CLIENT_HTTP_ERROR;
    else if(declared>LIZHI_RESPONSE_MAX_BYTES)result.status=LIZHI_CLIENT_BODY_TOO_LARGE;
    else if(!esp_http_client_is_complete_data_received(client)||(declared>=0&&(uint64_t)declared!=r->length))result.status=LIZHI_CLIENT_INCOMPLETE_BODY;
    else if(!r->json)result.status=LIZHI_CLIENT_FORMAT_ERROR;
    esp_http_client_cleanup(client);return result;
}
static size_t encode(const char *s,char *out,size_t capacity)
{
    size_t n=0;const char hex[]="0123456789ABCDEF";
    for(const unsigned char *p=(const unsigned char *)s;*p;++p){
        bool plain=(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-_.~",*p);
        size_t k=plain?1:3;if(n+k>=capacity)return 0;
        if(plain)out[n++]=*p;else{out[n++]='%';out[n++]=hex[*p>>4];out[n++]=hex[*p&15];}
    }
    out[n]=0;return n;
}
static auth_response_t *allocate(lizhi_cancel_fn cancel,void *context)
{
    auth_response_t *r=calloc(1,sizeof(*r));if(!r)return NULL;
    r->body=calloc(1,LIZHI_RESPONSE_MAX_BYTES+1);r->cancel=cancel;r->context=context;
    if(!r->body){free(r);return NULL;}return r;
}
static void release(auth_response_t *r)
{
    memset(r->body,0,LIZHI_RESPONSE_MAX_BYTES+1);free(r->body);memset(r,0,sizeof(*r));free(r);
}
lizhi_client_result_t lizhi_auth_create(lizhi_cancel_fn cancel,void *context,lizhi_auth_qr_t *out)
{
    lizhi_client_result_t result={.status=LIZHI_CLIENT_INVALID_ARGUMENT};
    if(!out)return result;
    memset(out,0,sizeof(*out));
    auth_response_t *r=allocate(cancel,context);if(!r){result.status=LIZHI_CLIENT_NO_MEMORY;return result;}
    result=request("https://growth.lizhi.fm/njQrcode/get","",r);
    if(result.status==LIZHI_CLIENT_OK)result.status=lizhi_auth_parse_qr(r->body,r->length,out);
    release(r);return result;
}
lizhi_client_result_t lizhi_auth_poll(const char *ticket,lizhi_cancel_fn cancel,void *context,
    lizhi_auth_state_t *state,lizhi_account_t *out)
{
    lizhi_client_result_t result={.status=LIZHI_CLIENT_INVALID_ARGUMENT};
    if(!state||!out||!ticket||!ticket[0]||strlen(ticket)>LIZHI_AUTH_TOKEN_BYTES)return result;
    memset(out,0,sizeof(*out));*state=LIZHI_AUTH_WAITING;
    auth_response_t *r=allocate(cancel,context);if(!r){result.status=LIZHI_CLIENT_NO_MEMORY;return result;}
    char *encoded=calloc(1,LIZHI_AUTH_TOKEN_BYTES*3+1), *post=calloc(1,LIZHI_AUTH_TOKEN_BYTES*3+100);
    char token[LIZHI_AUTH_TOKEN_BYTES+1]={0};
    if(!encoded||!post){result.status=LIZHI_CLIENT_NO_MEMORY;goto done;}
    if(!encode(ticket,encoded,LIZHI_AUTH_TOKEN_BYTES*3+1))goto done;
    snprintf(post,LIZHI_AUTH_TOKEN_BYTES*3+100,"tk=%s",encoded);
    result=request("https://growth.lizhi.fm/njQrcode/authLoginSuccCallBack",post,r);
    if(result.status!=LIZHI_CLIENT_OK)goto done;
    result.status=lizhi_auth_parse_poll(r->body,r->length,state,token,sizeof(token));
    if(result.status!=LIZHI_CLIENT_OK||*state!=LIZHI_AUTH_CONFIRMED)goto done;
    if(!encode(token,encoded,LIZHI_AUTH_TOKEN_BYTES*3+1)){result.status=LIZHI_CLIENT_FORMAT_ERROR;goto done;}
    snprintf(post,LIZHI_AUTH_TOKEN_BYTES*3+100,"https://nj.lizhi.fm/account/doLoginByQrcode?token=%s",encoded);
    result=request(post,"",r);
    if(result.status!=LIZHI_CLIENT_OK)goto done;
    result.status=lizhi_auth_parse_login(r->body,r->length);
    if(result.status!=LIZHI_CLIENT_OK)goto done;
    result=request("https://njnew.lizhi.fm/user/getCurrentUserInfo",NULL,r);
    if(result.status==LIZHI_CLIENT_OK)result.status=lizhi_auth_parse_account(r->body,r->length,out);
done:
    if(result.status!=LIZHI_CLIENT_OK){*state=LIZHI_AUTH_WAITING;memset(out,0,sizeof(*out));}
    memset(token,0,sizeof(token));
    if(encoded){memset(encoded,0,LIZHI_AUTH_TOKEN_BYTES*3+1);free(encoded);}
    if(post){memset(post,0,LIZHI_AUTH_TOKEN_BYTES*3+100);free(post);}
    release(r);return result;
}
