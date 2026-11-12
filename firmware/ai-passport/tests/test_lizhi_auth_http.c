#include "lizhi_auth.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct fake_http {esp_http_client_config_t config;} http;
static unsigned calls,cleanups,checks;
static int http_status,transport_error,header_error;
static bool complete,cancelled,cancel_during;
static const char *type,*encoding,*poll_body;
static char sent_cookie[2048];
static int phase;
static const char *qr_body="{\"rCode\":0,\"data\":{\"tk\":\"ticket\",\"url\":\"https://growth.lizhi.fm/static/qrcode-login/index.html?tk=ticket\",\"valid\":60}}";
esp_err_t esp_crt_bundle_attach(void *p){(void)p;return ESP_OK;}
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c)
{
    assert(c->crt_bundle_attach==esp_crt_bundle_attach&&c->disable_auto_redirect);
    assert(c->timeout_ms==15000);http.config=*c;++calls;
    sent_cookie[0]=0;
    if(strstr(c->url,"/njQrcode/get"))phase=0;
    else if(strstr(c->url,"authLoginSuccCallBack"))phase=1;
    else if(strstr(c->url,"doLoginByQrcode?token=a%2Bb%26c"))phase=2;
    else {assert(strstr(c->url,"/user/getCurrentUserInfo"));phase=3;}
    assert(c->method==(phase==3?HTTP_METHOD_GET:HTTP_METHOD_POST));
    return &http;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h,const char *key,const char *value)
{
    (void)h;
    if(!strcmp(key,"Cookie"))snprintf(sent_cookie,sizeof(sent_cookie),"%s",value);
    return header_error;
}
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t h,const char *data,int length)
{
    (void)h;assert(length==(int)strlen(data));
    if(phase==1)assert(!strcmp(data,"tk=ticket%2B%26"));
    return ESP_OK;
}
static int emit(int kind,const char *key,const char *value)
{
    esp_http_client_event_t e={.event_id=kind,.user_data=http.config.user_data,.header_key=key,
        .header_value=value,.data=value,.data_len=kind==HTTP_EVENT_ON_DATA?(int)strlen(value):0};
    return http.config.event_handler(&e);
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t h)
{
    (void)h;if(transport_error)return transport_error;
    int e=emit(HTTP_EVENT_ON_HEADER,"Content-Type",type);
    if(!e&&encoding)e=emit(HTTP_EVENT_ON_HEADER,"Content-Encoding",encoding);
    if(e)return e;
    if(phase==2){
        assert(!sent_cookie[0]);
        e=emit(HTTP_EVENT_ON_HEADER,"Set-Cookie","a=first; Path=/; Secure");
        if(!e)e=emit(HTTP_EVENT_ON_HEADER,"Set-Cookie","session=old; Path=/; Secure");
        if(!e)e=emit(HTTP_EVENT_ON_HEADER,"Set-Cookie","b=last; Path=/; Secure");
        if(!e)e=emit(HTTP_EVENT_ON_HEADER,"Set-Cookie","session=test-only; Path=/; Secure; HttpOnly");
    }
    if(phase==3)assert(!strcmp(sent_cookie,"a=first; b=last; session=test-only"));
    if(cancel_during)cancelled=true;
    if(!e)e=emit(HTTP_EVENT_ON_DATA,NULL,phase==0?qr_body:phase==1?poll_body:phase==2?"{\"code\":0}":
        "{\"rcode\":0,\"data\":{\"userId\":\"9223372036854775807\",\"name\":\"测试账号\"}}");
    return e;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h){(void)h;return http_status;}
int64_t esp_http_client_get_content_length(esp_http_client_handle_t h){(void)h;return -1;}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h){(void)h;return complete;}
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h){(void)h;++cleanups;return ESP_OK;}
static bool cancel(void *p){(void)p;return cancelled;}
static void reset(void)
{
    calls=cleanups=0;http_status=200;transport_error=header_error=0;complete=true;
    cancelled=cancel_during=false;type="application/json;charset=UTF-8";encoding=NULL;
    poll_body="{\"rCode\":2}";
}
static void create(lizhi_client_status_t expected)
{
    lizhi_auth_qr_t qr;lizhi_client_result_t r=lizhi_auth_create(cancel,NULL,&qr);
    assert(r.status==expected&&calls==cleanups);
    if(expected==LIZHI_CLIENT_OK)assert(!strcmp(qr.ticket,"ticket")&&qr.valid_seconds==60);
    else assert(!qr.ticket[0]&&!qr.url[0]);
    ++checks;
}
int main(void)
{
    reset();create(LIZHI_CLIENT_OK);
    reset();http_status=401;create(LIZHI_CLIENT_NEEDS_AUTH);
    reset();http_status=403;create(LIZHI_CLIENT_FORBIDDEN);
    reset();http_status=429;create(LIZHI_CLIENT_RATE_LIMITED);
    reset();http_status=302;create(LIZHI_CLIENT_REDIRECT_REJECTED);
    reset();http_status=500;create(LIZHI_CLIENT_HTTP_ERROR);
    reset();transport_error=ESP_FAIL;create(LIZHI_CLIENT_NETWORK_ERROR);
    reset();header_error=ESP_FAIL;create(LIZHI_CLIENT_NETWORK_ERROR);
    reset();complete=false;create(LIZHI_CLIENT_INCOMPLETE_BODY);
    reset();type="text/html";create(LIZHI_CLIENT_FORMAT_ERROR);
    reset();encoding="gzip";create(LIZHI_CLIENT_UNSUPPORTED_ENCODING);
    reset();cancelled=true;create(LIZHI_CLIENT_CANCELLED);assert(!calls);
    reset();cancel_during=true;create(LIZHI_CLIENT_CANCELLED);
    lizhi_account_t account;lizhi_auth_state_t state;lizhi_client_result_t r;
    reset();r=lizhi_auth_poll("ticket+&",cancel,NULL,&state,&account);
    assert(r.status==LIZHI_CLIENT_OK&&state==LIZHI_AUTH_WAITING&&!account.id[0]&&calls==1&&cleanups==1);++checks;
    reset();poll_body="{\"rCode\":1}";r=lizhi_auth_poll("ticket+&",cancel,NULL,&state,&account);
    assert(r.status==LIZHI_CLIENT_OK&&state==LIZHI_AUTH_EXPIRED&&calls==1&&cleanups==1);++checks;
    reset();poll_body="{\"rCode\":0,\"data\":{\"njLoginToken\":\"a+b&c\"}}";
    r=lizhi_auth_poll("ticket+&",cancel,NULL,&state,&account);
    assert(r.status==LIZHI_CLIENT_OK&&state==LIZHI_AUTH_CONFIRMED&&!strcmp(account.id,"9223372036854775807")&&calls==3&&cleanups==3);++checks;
    /* 下一次新流程不应继承先前的会话Cookie。 */
    reset();create(LIZHI_CLIENT_OK);assert(!sent_cookie[0]);
    printf("账号HTTP：%u通过/0失败\n",checks);
    return 0;
}
