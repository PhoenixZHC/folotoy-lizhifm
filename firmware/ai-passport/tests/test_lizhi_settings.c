#include "lizhi_settings.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    unsigned passed=0;
#define CHECK(x) do{assert(x);++passed;}while(0)
    lizhi_settings_t s;lizhi_settings_defaults(&s);
    CHECK(lizhi_settings_valid(&s)&&!s.bound&&s.volume==25);
    lizhi_account_t user={.id="123",.name="用户"};
    CHECK(lizhi_settings_bind(&s,&user)&&s.generation==2&&s.bound&&lizhi_settings_valid(&s));
    s.volume=75;CHECK(!lizhi_settings_valid(&s));lizhi_settings_seal(&s);CHECK(lizhi_settings_valid(&s));
    lizhi_settings_t other=s;other.account.id[0]='4';CHECK(!lizhi_settings_valid(&other));
    lizhi_settings_unbind(&s);CHECK(!s.bound&&!s.account.id[0]&&s.volume==75&&s.generation==3&&lizhi_settings_valid(&s));
    strcpy(user.id,"0");CHECK(!lizhi_settings_bind(&s,&user));
    strcpy(user.id,"9223372036854775808");CHECK(!lizhi_settings_bind(&s,&user));
    s.volume=101;lizhi_settings_seal(&s);CHECK(!lizhi_settings_valid(&s));
    lizhi_settings_defaults(&s);s.timeout_seconds=7;lizhi_settings_seal(&s);CHECK(!lizhi_settings_valid(&s));
    printf("持久配置：%u通过/0失败\n",passed);
}
