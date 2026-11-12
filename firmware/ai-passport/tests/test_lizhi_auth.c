#include "lizhi_auth.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned passed;
#define CHECK(x) do { assert(x); ++passed; } while(0)
int main(void)
{
    lizhi_auth_qr_t qr;
    const char *valid="{\"rCode\":0,\"data\":{\"valid\":60,\"tk\":\"ticket\",\"url\":\"https://growth.lizhi.fm/static/qrcode-login/index.html?tk=x\"}}";
    CHECK(lizhi_auth_parse_qr(valid,strlen(valid),&qr)==LIZHI_CLIENT_OK);
    CHECK(qr.valid_seconds==60&&!strcmp(qr.ticket,"ticket"));
    for(size_t i=0;i<strlen(valid);++i)CHECK(lizhi_auth_parse_qr(valid,i,&qr)!=LIZHI_CLIENT_OK);
    const char *bad[]={"{}","[]","{\"rCode\":0,\"rCode\":0}","{\"rCode\":0,\"data\":{\"valid\":301,\"tk\":\"t\",\"url\":\"https://evil/x\"}}"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i)CHECK(lizhi_auth_parse_qr(bad[i],strlen(bad[i]),&qr)==LIZHI_CLIENT_FORMAT_ERROR);
    lizhi_auth_state_t state;char token[513];
    const char *wait="{\"rCode\":2}",*expired="{\"rCode\":1}",*confirm="{\"rCode\":0,\"data\":{\"njLoginToken\":\"one-shot\"}}";
    CHECK(lizhi_auth_parse_poll(wait,strlen(wait),&state,token,sizeof(token))==LIZHI_CLIENT_OK&&state==LIZHI_AUTH_WAITING&&!token[0]);
    CHECK(lizhi_auth_parse_poll(expired,strlen(expired),&state,token,sizeof(token))==LIZHI_CLIENT_OK&&state==LIZHI_AUTH_EXPIRED);
    CHECK(lizhi_auth_parse_poll(confirm,strlen(confirm),&state,token,sizeof(token))==LIZHI_CLIENT_OK&&state==LIZHI_AUTH_CONFIRMED&&!strcmp(token,"one-shot"));
    CHECK(lizhi_auth_parse_poll(confirm,strlen(confirm),&state,token,2)==LIZHI_CLIENT_FORMAT_ERROR);
    lizhi_account_t account;
    const char *user="{\"rcode\":0,\"data\":{\"userId\":\"5036275272706175532\",\"name\":\"测试用户\"}}";
    CHECK(lizhi_auth_parse_account(user,strlen(user),&account)==LIZHI_CLIENT_OK&&!strcmp(account.id,"5036275272706175532"));
    const char *invalid[]={"{\"rcode\":0,\"data\":{\"userId\":5036275272706175532}}","{\"rcode\":0,\"data\":{\"userId\":\"0\"}}","{\"rcode\":0,\"data\":{\"userId\":\"9223372036854775808\"}}"};
    for(size_t i=0;i<3;++i)CHECK(lizhi_auth_parse_account(invalid[i],strlen(invalid[i]),&account)==LIZHI_CLIENT_FORMAT_ERROR&&!account.id[0]);
    const char *login_ok="{\"code\":0}",*login_bad="{\"code\":0,\"code\":-100}";
    CHECK(lizhi_auth_parse_login(login_ok,strlen(login_ok))==LIZHI_CLIENT_OK);
    CHECK(lizhi_auth_parse_login(login_bad,strlen(login_bad))==LIZHI_CLIENT_FORMAT_ERROR);
    const char *nul="{\"rcode\":0,\"data\":{\"userId\":\"123\\u0000456\"}}";
    CHECK(lizhi_auth_parse_account(nul,strlen(nul),&account)==LIZHI_CLIENT_FORMAT_ERROR);
    const char *dupe="{\"rcode\":0,\"data\":{\"userId\":\"123\",\"name\":\"a\",\"name\":\"b\"}}";
    CHECK(lizhi_auth_parse_account(dupe,strlen(dupe),&account)==LIZHI_CLIENT_FORMAT_ERROR);
    char broken[256];snprintf(broken,sizeof(broken),"%s",user);broken[20]=(char)0xff;
    CHECK(lizhi_auth_parse_account(broken,strlen(broken),&account)==LIZHI_CLIENT_FORMAT_ERROR);
    printf("账号协议：%u通过/0失败\n",passed);
}
