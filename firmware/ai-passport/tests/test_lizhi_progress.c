#include "lizhi_progress.h"
#include <stdio.h>
#include <string.h>
static unsigned checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; fprintf(stderr,"line %d: %s\n",__LINE__,#x); } } while (0)
int main(void)
{
    lizhi_progress_t r = {.author="5036275272706175532",.episode={.id="3243638952740496390",.title="真实节目标题"},.position_ms=60000,
        .checkpoint={.resource={.total_bytes=40964473,.validator="\"sample-etag\"",.is_etag=true},.frame={640000,40000000}}};
    CHECK(!lizhi_progress_valid(&r,r.author));
    lizhi_progress_seal(&r); CHECK(lizhi_progress_valid(&r,r.author));
    CHECK(!lizhi_progress_valid(&r,"999"));
    lizhi_progress_t copy = r; copy.position_ms++; CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; copy.version++; CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; strcpy(copy.scope,"account"); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; copy.checkpoint.frame.offset=copy.checkpoint.resource.total_bytes; lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; copy.position_ms=10000; lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; copy.position_ms=150000; lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; copy.position_ms=0; lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; memset(copy.episode.title,'a',sizeof(copy.episode.title)); lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; strcpy(copy.episode.id,"../123"); lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    copy=r; memset(copy.checkpoint.resource.validator,0,sizeof(copy.checkpoint.resource.validator)); lizhi_progress_seal(&copy); CHECK(!lizhi_progress_valid(&copy,copy.author));
    lizhi_scoped_progress_t scoped={.owner="123",.record=r};
    lizhi_scoped_progress_seal(&scoped);
    CHECK(lizhi_scoped_progress_valid(&scoped,"123"));
    CHECK(!lizhi_scoped_progress_valid(&scoped,"124"));
    scoped.owner[2]='4';CHECK(!lizhi_scoped_progress_valid(&scoped,"124"));
    scoped.owner[2]='3';scoped.record.position_ms++;CHECK(!lizhi_scoped_progress_valid(&scoped,"123"));
    printf("lizhi progress: %u passed, %u failed\n",checks-failed,failed);
    return failed ? 1 : 0;
}
