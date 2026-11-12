#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lizhi_detail.h"

static const char author[] = "5036275272706175532";
/* 真实列表的公开字段投影；异常输入仅由本机测试编译。 */
static const char row[] = "{\"userInfo\":{\"userId\":\"5036275272706175532\"},"
    "\"voiceInfo\":{\"voiceId\":\"3243638952740496390\",\"duration\":2551,"
    "\"name\":\"今时不同往日，交新朋友得先准入考试\"}}";

int main(int argc, char **argv)
{
    lizhi_episode_page_t out;
    unsigned passed = 0;
    char body[17000];
    snprintf(body, sizeof(body), "{\"code\":0,\"data\":[%s,%s]}", row, row);
    assert(lizhi_parse_program_page(body, strlen(body), author, &out) == LIZHI_PARSE_OK);
    assert(out.received_count == 2 && out.count == 1 && out.duplicates == 1);
    assert(strcmp(out.items[0].episode_id, "3243638952740496390") == 0);
    assert(out.items[0].duration_known && out.items[0].duration_seconds == 2551);
    ++passed;
    assert(lizhi_parse_program_page(body, strlen(body), "9", &out) == LIZHI_PARSE_SCHEMA_CHANGED);
    lizhi_episode_page_t empty = {0};
    assert(memcmp(&out, &empty, sizeof(out)) == 0);
    ++passed;
    snprintf(body, sizeof(body), "{\"code\":0,\"data\":[%s,%s,%s]}", row, row, row);
    assert(lizhi_parse_program_page(body, strlen(body), author, &out) == LIZHI_PARSE_TOO_LARGE);
    ++passed;
    const char *bad[] = {"{\"code\":0,\"data\":{}}", "{\"code\":0,\"data\":[null]}",
                         "{\"code\":0,\"data\":[{}]}", "{\"code\":false,\"data\":[]}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        assert(lizhi_parse_program_page(bad[i], strlen(bad[i]), author, &out) == LIZHI_PARSE_SCHEMA_CHANGED);
        ++passed;
    }
    const char *none = "{\"code\":0,\"data\":[]}";
    assert(lizhi_parse_program_page(none, strlen(none), author, &out) == LIZHI_PARSE_OK);
    assert(out.received_count == 0 && out.count == 0);
    ++passed;
    const char *denied = "{\"code\":403,\"data\":[]}";
    assert(lizhi_parse_program_page(denied, strlen(denied), author, &out) == LIZHI_PARSE_SERVICE_ERROR);
    ++passed;
    assert(lizhi_parse_program_page(none, strlen(none), "-1", &out) == LIZHI_PARSE_INVALID_ARGUMENT);
    ++passed;
    for (int i = 1; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb");
        assert(file);
        size_t length = fread(body, 1, sizeof(body), file);
        assert(feof(file) && !ferror(file));
        fclose(file);
        assert(lizhi_parse_program_page(body, length, author, &out) == LIZHI_PARSE_OK);
        const size_t expected[] = {2, 2, 1, 0};
        assert(i <= 4 && out.received_count == expected[i - 1] && out.count == expected[i - 1]);
        if (i == 1) {
            assert(strcmp(out.items[0].episode_id, "3244951408539225606") == 0);
            assert(strcmp(out.items[1].episode_id, "3243638952740496390") == 0);
        }
        ++passed;
    }
    printf("荔枝列表解析：%u 通过，0 失败\n", passed);
    return 0;
}
