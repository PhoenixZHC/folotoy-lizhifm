#include "lizhi_list_loader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* 只编入主机测试：检查四行视图对两条网络分页的真实调用约束。 */
static unsigned passed, calls, fail_call, cancel_after, duplicate_call;
static size_t sizes[2];
static bool last[2];
static uint32_t first_page;
static char cursors[2][65];
#define CHECK(x) do { assert(x); ++passed; } while (0)

static void reset(void)
{
    calls = fail_call = duplicate_call = 0; cancel_after = 99;
    sizes[0] = sizes[1] = 2; last[0] = last[1] = false;
    first_page = 1; strcpy(cursors[0], "2"); strcpy(cursors[1], "4");
}
static bool cancelled(void *context) { (void)context; return calls >= cancel_after; }
lizhi_client_result_t lizhi_client_get_page(const char *author, uint32_t page,
    lizhi_cancel_fn cancel, void *context, lizhi_episode_page_t *out)
{
    (void)cancel; (void)context;
    assert(!strcmp(author, "123") && calls < 2 && page == first_page+calls);
    unsigned call = calls++;
    if (calls == fail_call) return (lizhi_client_result_t){.status=LIZHI_CLIENT_NETWORK_ERROR, .http_status=503};
    memset(out, 0, sizeof(*out)); out->count = sizes[call];
    for (size_t i = 0; i < sizes[call] && i < LIZHI_PAGE_MAX_ITEMS; ++i) {
        snprintf(out->items[i].episode_id, sizeof(out->items[i].episode_id), "%u", duplicate_call == calls ? 1 : (unsigned)(call*2+i+1));
        snprintf(out->items[i].title, sizeof(out->items[i].title), "节目%u", (unsigned)(call*2+i+1));
        out->items[i].duration_known = true; out->items[i].duration_seconds = 60+i;
        out->items[i].created_at_known = true; out->items[i].created_at = 1700000000+i;
    }
    return (lizhi_client_result_t){.status=LIZHI_CLIENT_OK};
}
lizhi_client_result_t lizhi_client_get_follows(const char *owner, const char *cursor,
    lizhi_cancel_fn cancel, void *context, lizhi_follow_page_t *out)
{
    (void)cancel; (void)context;
    assert(!strcmp(owner, "123") && calls < 2);
    assert(!strcmp(cursor, calls ? cursors[0] : ""));
    unsigned call = calls++;
    if (calls == fail_call) return (lizhi_client_result_t){.status=LIZHI_CLIENT_FORBIDDEN, .http_status=403};
    memset(out, 0, sizeof(*out)); out->count = sizes[call]; out->last = last[call];
    strcpy(out->cursor, cursors[call]);
    for (size_t i = 0; i < sizes[call] && i < LIZHI_PAGE_MAX_ITEMS; ++i) {
        snprintf(out->authors[i].id, sizeof(out->authors[i].id), "%u", duplicate_call == calls ? 1 : (unsigned)(call*2+i+1));
        snprintf(out->authors[i].name, sizeof(out->authors[i].name), "博主%u", (unsigned)(call*2+i+1));
    }
    return (lizhi_client_result_t){.status=LIZHI_CLIENT_OK};
}

static void programs(lizhi_client_status_t expected, size_t expected_count, unsigned expected_calls)
{
    lizhi_row_t rows[4], empty[4] = {0}; size_t count = 99;
    memset(rows, 0xa5, sizeof(rows));
    uint32_t page = (first_page+1)/2;
    lizhi_client_result_t result = lizhi_load_program_view("123", page, cancelled, NULL, rows, &count);
    CHECK(result.status == expected && count == expected_count && calls == expected_calls);
    if (expected != LIZHI_CLIENT_OK) CHECK(!memcmp(rows, empty, sizeof(rows)));
    if (expected_count == 4) {
        CHECK(!strcmp(rows[2].id, "3") && !strcmp(rows[3].title, "节目4"));
        CHECK(rows[3].duration_known && rows[3].seconds == 61 && rows[3].created == 1700000001);
    }
}
static void follows(lizhi_client_status_t expected, size_t count, unsigned expected_calls)
{
    lizhi_follow_view_t out, empty = {0}; memset(&out, 0xa5, sizeof(out));
    lizhi_client_result_t result = lizhi_load_follow_view("123", "", cancelled, NULL, &out);
    CHECK(result.status == expected && out.count == count && calls == expected_calls);
    if (expected != LIZHI_CLIENT_OK) CHECK(!memcmp(&out, &empty, sizeof(out)));
    else {
        CHECK(out.last == last[expected_calls-1]);
        CHECK(!strcmp(out.cursor, cursors[expected_calls-1]));
        if (count == 4) CHECK(!strcmp(out.authors[3].id,"4"));
    }
}
int main(void)
{
    reset(); programs(LIZHI_CLIENT_OK,4,2);
    reset(); first_page=3; programs(LIZHI_CLIENT_OK,4,2);
    reset(); first_page=9999; programs(LIZHI_CLIENT_OK,4,2);
    reset(); sizes[0]=0; programs(LIZHI_CLIENT_OK,0,1);
    reset(); sizes[1]=0; programs(LIZHI_CLIENT_OK,2,2);
    reset(); sizes[0]=1; programs(LIZHI_CLIENT_OK,3,2);
    reset(); fail_call=1; programs(LIZHI_CLIENT_NETWORK_ERROR,0,1);
    reset(); fail_call=2; programs(LIZHI_CLIENT_NETWORK_ERROR,0,2);
    reset(); cancel_after=0; programs(LIZHI_CLIENT_CANCELLED,0,0);
    reset(); cancel_after=1; programs(LIZHI_CLIENT_CANCELLED,0,1);
    reset(); cancel_after=2; programs(LIZHI_CLIENT_CANCELLED,0,2);
    reset(); duplicate_call=2; programs(LIZHI_CLIENT_FORMAT_ERROR,0,2);
    reset(); sizes[1]=3; programs(LIZHI_CLIENT_FORMAT_ERROR,0,2);
    reset(); follows(LIZHI_CLIENT_OK,4,2);
    reset(); last[0]=true; follows(LIZHI_CLIENT_OK,2,1);
    reset(); sizes[0]=0; last[0]=true; follows(LIZHI_CLIENT_OK,0,1);
    reset(); last[1]=true; sizes[1]=1; follows(LIZHI_CLIENT_OK,3,2);
    reset(); last[1]=true; sizes[1]=0; follows(LIZHI_CLIENT_OK,2,2);
    reset(); fail_call=1; follows(LIZHI_CLIENT_FORBIDDEN,0,1);
    reset(); fail_call=2; follows(LIZHI_CLIENT_FORBIDDEN,0,2);
    reset(); cancel_after=0; follows(LIZHI_CLIENT_CANCELLED,0,0);
    reset(); cancel_after=1; follows(LIZHI_CLIENT_CANCELLED,0,1);
    reset(); cancel_after=2; follows(LIZHI_CLIENT_CANCELLED,0,2);
    reset(); duplicate_call=2; follows(LIZHI_CLIENT_FORMAT_ERROR,0,2);
    reset(); strcpy(cursors[1],"2"); follows(LIZHI_CLIENT_FORMAT_ERROR,0,2);
    reset(); cursors[1][0]=0; follows(LIZHI_CLIENT_FORMAT_ERROR,0,2);
    printf("四行列表聚合：%u通过/0失败\n",passed);
}
