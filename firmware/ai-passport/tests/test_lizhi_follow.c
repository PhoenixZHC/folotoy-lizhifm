#include "lizhi_follow.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static unsigned passed;
static void check(const unsigned char *b, size_t n, lizhi_client_status_t expected)
{
    lizhi_follow_page_t out; int32_t code;
    memset(&out, 0xa5, sizeof(out));
    assert(lizhi_follow_parse(b, n, "123", &out, &code) == expected);
    if (expected != LIZHI_CLIENT_OK) {
        lizhi_follow_page_t empty = {0}; assert(!memcmp(&empty, &out, sizeof(out)));
    }
    passed++;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--wire")) {
        char owner[34]; unsigned char body[LIZHI_RESPONSE_MAX_BYTES+1];
        if (!fgets(owner, sizeof(owner), stdin)) return 1;
        owner[strcspn(owner, "\n")] = 0;
        size_t n = fread(body, 1, sizeof(body), stdin);
        lizhi_follow_page_t out; int32_t code;
        lizhi_client_status_t status = lizhi_follow_parse(body, n, owner, &out, &code);
        printf("status=%d count=%u last=%u\n", status, (unsigned)out.count, out.last);
        return status != LIZHI_CLIENT_OK;
    }
    /* 全部仅为主机测试结构样本，不进入生产入口。 */
    unsigned char empty[] = {16,0,48,1};
    unsigned char one[] = {16,0,26,7,10,5,8,42,18,1,'A',42,1,'2',48,0};
    unsigned char duplicate[64];
    check(empty, sizeof(empty), LIZHI_CLIENT_OK);
    check(one, sizeof(one), LIZHI_CLIENT_OK);
    for (size_t n = 0; n < sizeof(one); ++n) check(one, n, LIZHI_CLIENT_FORMAT_ERROR);
    memcpy(duplicate, one, 11); memcpy(duplicate+11, one+2, 9); memcpy(duplicate+20, one+11, 5);
    check(duplicate, 25, LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char badbool[] = {16,0,48,2}; check(badbool, sizeof(badbool), LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char code[] = {16,1}; check(code, sizeof(code), LIZHI_CLIENT_SERVICE_ERROR);
    unsigned char largecode[] = {16,128,128,128,128,16,48,1};
    check(largecode, sizeof(largecode), LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char dupe_code[] = {16,0,16,0,48,1}; check(dupe_code, sizeof(dupe_code), LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char wrongwire[] = {16,0,40,1,48,1}; check(wrongwire, sizeof(wrongwire), LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char overflow[] = {16,128,128,128,128,128,128,128,128,128,2}; check(overflow, sizeof(overflow), LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char badname[sizeof(one)]; memcpy(badname, one, sizeof(one)); badname[10] = 0;
    check(badname, sizeof(badname), LIZHI_CLIENT_FORMAT_ERROR);
    badname[10] = 0xc0; check(badname, sizeof(badname), LIZHI_CLIENT_FORMAT_ERROR);
    unsigned char unknown[] = {16,0,48,1,0x79,1,2,3,4,5,6,7,8}; check(unknown, sizeof(unknown), LIZHI_CLIENT_OK);
    unsigned char post[128];
    assert(lizhi_follow_encode("123", "", post, sizeof(post)) == 10); passed++;
    assert(!lizhi_follow_encode("0", "", post, sizeof(post))); passed++;
    assert(!lizhi_follow_encode("9223372036854775808", "", post, sizeof(post))); passed++;
    assert(!lizhi_follow_encode("1/2", "", post, sizeof(post))); passed++;
    assert(!lizhi_follow_encode("123", "", post, 10)); passed++;
    char cursor[67]; memset(cursor, 'a', sizeof(cursor)); cursor[66] = 0;
    assert(!lizhi_follow_encode("123", cursor, post, sizeof(post))); passed++;
    printf("关注解析：%u通过/0失败\n", passed);
    return 0;
}
