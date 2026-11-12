#include "lizhi_library.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { assert(x); ++checks; } while(0)
int main(void)
{
    size_t bytes=lizhi_library_bytes(14);
    lizhi_library_t *s=calloc(1,bytes);
    CHECK(s!=NULL);
    s->count=14;strcpy(s->owner,"123");
    for(unsigned i=0;i<14;++i){
        snprintf(s->authors[i].id,sizeof(s->authors[i].id),"%u",i+100);
        snprintf(s->authors[i].name,sizeof(s->authors[i].name),"测试博主%u",i);
    }
    lizhi_library_seal(s);
    CHECK(lizhi_library_valid(s,bytes,"123"));
    CHECK(!lizhi_library_valid(s,bytes,"124"));
    for(size_t n=0;n<bytes;++n)CHECK(!lizhi_library_valid(s,n,"123"));
    lizhi_follow_view_t view;
    for(unsigned page=1;page<=4;++page){
        CHECK(lizhi_library_view(s,page,&view));
        CHECK(view.count==(page==4?2:4));
        CHECK(view.last==(page==4));
        CHECK(!strcmp(view.authors[0].id,s->authors[(page-1)*4].id));
        CHECK(page==4?!view.cursor[0]:view.cursor[0]!=0);
    }
    CHECK(!lizhi_library_view(s,0,&view));
    CHECK(!lizhi_library_view(s,5,&view));
    s->owner[2]='4';CHECK(!lizhi_library_valid(s,bytes,"124"));s->owner[2]='3';
    strcpy(s->authors[1].id,s->authors[0].id);lizhi_library_seal(s);
    CHECK(!lizhi_library_valid(s,bytes,"123"));
    s->count=0;lizhi_library_seal(s);
    CHECK(lizhi_library_valid(s,lizhi_library_bytes(0),"123"));
    CHECK(lizhi_library_view(s,1,&view)&&view.count==0&&view.last);
    s->count=4;lizhi_library_seal(s);
    CHECK(!lizhi_library_view(s,2,&view));
    CHECK(lizhi_library_bytes(129)==0);
    free(s);
    printf("关注缓存：%u通过/0失败\n",checks);
    return 0;
}
