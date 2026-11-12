#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lizhi_detail.h"
#include "cJSON.h"

/* 用户真实响应的公开字段投影；不含 token、HTML 简介或账号字段。 */
static const char sample[] =
    "{\"code\":0,\"data\":{\"userVoice\":{"
    "\"userInfo\":{\"userId\":\"5036275272706175532\",\"band\":\"107790402\"},"
    "\"voiceInfo\":{\"voiceId\":\"3243638952740496390\","
    "\"jockeyId\":\"5036275272706175532\",\"name\":\"今时不同往日，交新朋友得先准入考试\","
    "\"duration\":2551,\"createTime\":1793664000},"
    "\"voicePlayProperty\":{\"playAccessProperty\":{\"accessPermission\":2,\"accessType\":0},"
    "\"trackUrl\":\"http://cdn5.lizhi.fm/audio/2026/11/03/3243638952740496390_hd.mp3\"}},"
    "\"voiceIds\":[\"3244951408539225606\",\"3242239875426326022\"]}}";
static const char id[] = "3243638952740496390";
static unsigned passed;

static void *fail_allocation(size_t size)
{
    (void)size;
    return NULL;
}

static void replace_once(char *out, size_t size, const char *needle, const char *replacement)
{
    const char *hit = strstr(sample, needle);
    assert(hit);
    int n = snprintf(out, size, "%.*s%s%s", (int)(hit - sample), sample,
                     replacement, hit + strlen(needle));
    assert(n >= 0 && (size_t)n < size);
}

static void check_modified(const char *needle, const char *replacement, lizhi_parse_result_t expected)
{
    char body[18000];
    lizhi_episode_detail_t result;
    memset(&result, 0x7f, sizeof(result));
    replace_once(body, sizeof(body), needle, replacement);
    assert(lizhi_parse_detail(body, strlen(body), id, &result) == expected);
    if (expected != LIZHI_PARSE_OK) {
        lizhi_episode_detail_t empty = {0};
        assert(memcmp(&result, &empty, sizeof(result)) == 0);
    }
    ++passed;
}

int main(int argc, char **argv)
{
    lizhi_episode_detail_t result;
    assert(lizhi_parse_detail(sample, sizeof(sample) - 1, id, &result) == LIZHI_PARSE_OK);
    assert(strcmp(result.episode_id, id) == 0);
    assert(strcmp(result.author_id, "5036275272706175532") == 0);
    assert(strcmp(result.title, "今时不同往日，交新朋友得先准入考试") == 0);
    assert(result.duration_known && result.duration_seconds == 2551);
    assert(result.created_at_known && result.created_at == 1793664000);
    assert(result.has_media_url && result.media_requires_https_validation);
    assert(strncmp(result.media_url, "http://cdn5.lizhi.fm/", 21) == 0);
    assert(result.access_permission_known && result.access_permission == 2);
    assert(result.access_type_known && result.access_type == 0);
    ++passed;

    /* 真实60:38节目的结构投影；仅标题用隔离测试文本替代。 */
    const char *paid="{\"code\":0,\"data\":{\"payProperty\":{\"price\":35},\"userVoice\":{"
        "\"userInfo\":{\"userId\":\"5036275272706175532\"},\"userHadBuy\":false,"
        "\"voiceInfo\":{\"voiceId\":\"3242239875426326022\",\"name\":\"测试标题\",\"duration\":3638},"
        "\"voicePlayProperty\":{\"playAccessProperty\":{\"accessPermission\":4,\"accessType\":2}}}}}";
    assert(lizhi_parse_detail(paid,strlen(paid),"3242239875426326022",&result)==LIZHI_PARSE_OK);
    assert(result.payment_required_without_media&&!result.has_media_url);++passed;
    cJSON *paid_json=cJSON_Parse(paid);
    cJSON *paid_data=cJSON_GetObjectItemCaseSensitive(paid_json,"data");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(paid_data,"payProperty"),"price"),0);
    char *free_variant=cJSON_PrintUnformatted(paid_json);
    assert(lizhi_parse_detail(free_variant,strlen(free_variant),"3242239875426326022",&result)==LIZHI_PARSE_OK);
    assert(!result.payment_required_without_media&&!result.has_media_url);++passed;
    free(free_variant);cJSON_Delete(paid_json);
    assert(lizhi_parse_detail(sample,sizeof(sample)-1,id,&result)==LIZHI_PARSE_OK);
    assert(!result.payment_required_without_media&&result.has_media_url);++passed;

    /* 数字 ID 不经 double 接受，避免静默丢精度。 */
    check_modified("\"voiceId\":\"3243638952740496390\"", "\"voiceId\":3243638952740496390", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"userId\":\"5036275272706175532\"", "\"userId\":\"0\"", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"jockeyId\":\"5036275272706175532\"", "\"jockeyId\":\"9\"", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"voiceId\":\"3243638952740496390\"", "\"voiceId\":\"1\"", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"duration\":2551", "\"duration\":-1", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"duration\":2551", "\"duration\":1.5", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"duration\":2551", "\"duration\":true", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"duration\":2551", "\"duration\":4294967296", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"duration\":2551", "\"duration\":01", LIZHI_PARSE_INVALID_JSON);
    check_modified("\"duration\":2551", "\"duration\":1.", LIZHI_PARSE_INVALID_JSON);
    check_modified("\"duration\":2551", "\"duration\":null", LIZHI_PARSE_OK);
    check_modified("\"duration\":2551", "\"unusedDuration\":2551", LIZHI_PARSE_OK);
    check_modified("\"duration\":2551", "\"duration\":0", LIZHI_PARSE_OK);
    check_modified("\"code\":0", "\"code\":403", LIZHI_PARSE_SERVICE_ERROR);
    check_modified("\"code\":0", "\"code\":true", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"code\":0", "\"code\":0,\"code\":403", LIZHI_PARSE_INVALID_JSON);
    check_modified("\"duration\":2551", "\"duration\":2551,\"duration\":1", LIZHI_PARSE_INVALID_JSON);
    check_modified("\"voiceInfo\":{", "\"voiceInfo\":{\"unknown\":\"\\u0000hidden\",", LIZHI_PARSE_INVALID_JSON);
    check_modified("http://cdn5.lizhi.fm/", "https://cdn5.lizhi.fm/", LIZHI_PARSE_OK);
    check_modified("http://cdn5.lizhi.fm/", "http://user:pass@cdn5.lizhi.fm/", LIZHI_PARSE_INVALID_URL);
    check_modified("http://cdn5.lizhi.fm/", "file://cdn5.lizhi.fm/", LIZHI_PARSE_INVALID_URL);
    check_modified("http://cdn5.lizhi.fm/", "http://cdn5.lizhi.fm\\n/", LIZHI_PARSE_INVALID_URL);
    check_modified("\"accessPermission\":2", "\"accessPermission\":1.5", LIZHI_PARSE_SCHEMA_CHANGED);
    check_modified("\"voicePlayProperty\":{", "\"ignoredPlayProperty\":{", LIZHI_PARSE_OK);
    check_modified("\"trackUrl\":\"http://cdn5.lizhi.fm/audio/2026/11/03/3243638952740496390_hd.mp3\"",
                   "\"trackUrl\":\"\"", LIZHI_PARSE_OK);

    /* 不要求 NUL 终止，拒绝尾随垃圾和中断数据。 */
    char body[18000];
    memcpy(body, sample, sizeof(sample) - 1);
    body[sizeof(sample) - 1] = 'X';
    assert(lizhi_parse_detail(body, sizeof(sample), id, &result) == LIZHI_PARSE_JSON_FAILED);
    assert(lizhi_parse_detail(sample, sizeof(sample) - 4, id, &result) == LIZHI_PARSE_INVALID_JSON);
    assert(lizhi_parse_detail("<html>403</html>", strlen("<html>403</html>"), id, &result) == LIZHI_PARSE_INVALID_JSON);
    memset(body, ' ', sizeof(body));
    assert(lizhi_parse_detail(body, sizeof(body), id, &result) == LIZHI_PARSE_TOO_LARGE);
    assert(lizhi_parse_detail(NULL, 0, id, &result) == LIZHI_PARSE_INVALID_ARGUMENT);
    assert(lizhi_parse_detail(sample, sizeof(sample) - 1, "-1", &result) == LIZHI_PARSE_INVALID_ARGUMENT);
    passed += 6;

    /* 分配失败注入仅在主机测试内，不改变生产 cJSON 全局配置。 */
    cJSON_Hooks hooks = {fail_allocation, free};
    cJSON_InitHooks(&hooks);
    assert(lizhi_parse_detail(sample, sizeof(sample) - 1, id, &result) == LIZHI_PARSE_JSON_FAILED);
    cJSON_InitHooks(NULL);
    assert(!result.episode_id[0] && !result.has_media_url);
    ++passed;

    /* UTF-8 标题截断到完整字符，URL 超长必须拒绝。 */
    char title[260];
    memset(title, 'a', 191);
    memcpy(title + 191, "中文", 7);
    char name[300];
    snprintf(name, sizeof(name), "\"name\":\"%s\"", title);
    replace_once(body, sizeof(body), "\"name\":\"今时不同往日，交新朋友得先准入考试\"", name);
    assert(lizhi_parse_detail(body, strlen(body), id, &result) == LIZHI_PARSE_OK);
    assert(result.title_truncated && strlen(result.title) == 191);
    char long_url[2200];
    memset(long_url, 'a', sizeof(long_url));
    memcpy(long_url, "https://example.com/", 20);
    long_url[2199] = 0;
    replace_once(body, sizeof(body), "http://cdn5.lizhi.fm/audio/2026/11/03/3243638952740496390_hd.mp3", long_url);
    assert(lizhi_parse_detail(body, strlen(body), id, &result) == LIZHI_PARSE_INVALID_URL);
    passed += 2;

    replace_once(body, sizeof(body), "\"duration\":2551", "\"unusedDuration\":2551");
    assert(lizhi_parse_detail(body, strlen(body), id, &result) == LIZHI_PARSE_OK);
    assert(!result.duration_known);
    replace_once(body, sizeof(body), "\"duration\":2551", "\"duration\":0");
    assert(lizhi_parse_detail(body, strlen(body), id, &result) == LIZHI_PARSE_OK);
    assert(result.duration_known && result.duration_seconds == 0);
    replace_once(body, sizeof(body), "\"voicePlayProperty\":{", "\"ignoredPlayProperty\":{");
    assert(lizhi_parse_detail(body, strlen(body), id, &result) == LIZHI_PARSE_OK);
    assert(!result.has_media_url && !result.media_requires_https_validation);
    passed += 3;

    char many[4000] = {0};
    strcpy(many, "[");
    for (unsigned i = 0; i < 600; ++i) strcat(many, i ? ",true" : "true");
    strcat(many, "]");
    assert(lizhi_parse_detail(many, strlen(many), id, &result) == LIZHI_PARSE_INVALID_JSON);
    ++passed;

    /* 可直接读取本机用户响应验证，不复制到仓库且不打印内容。 */
    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");
        assert(file);
        size_t length = fread(body, 1, sizeof(body), file);
        assert(!ferror(file) && feof(file));
        fclose(file);
        assert(lizhi_parse_detail(body, length, id, &result) == LIZHI_PARSE_OK);
        assert(result.duration_seconds == 2551);
        ++passed;
    }
    printf("荔枝详情解析：%u 通过，0 失败\n", passed);
    return 0;
}
