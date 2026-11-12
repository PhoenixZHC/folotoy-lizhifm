#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIZHI_RESPONSE_MAX_BYTES 16384
#define LIZHI_ID_BYTES 32
#define LIZHI_TITLE_BYTES 192
#define LIZHI_URL_BYTES 2048
#define LIZHI_PAGE_MAX_ITEMS 2

typedef enum {
    LIZHI_PARSE_OK = 0,
    LIZHI_PARSE_INVALID_ARGUMENT,
    LIZHI_PARSE_TOO_LARGE,
    LIZHI_PARSE_INVALID_JSON,
    LIZHI_PARSE_SCHEMA_CHANGED,
    LIZHI_PARSE_SERVICE_ERROR,
    LIZHI_PARSE_INVALID_URL,
    LIZHI_PARSE_NO_MEMORY,
    /* cJSON 无法区分语法错误与内部节点分配失败，不伪装成特定原因。 */
    LIZHI_PARSE_JSON_FAILED,
} lizhi_parse_result_t;

/* 仅表示响应内容；权限枚举尚未验证，解析成功不等于可播放。 */
typedef struct {
    char episode_id[LIZHI_ID_BYTES + 1];
    char author_id[LIZHI_ID_BYTES + 1];
    char title[LIZHI_TITLE_BYTES + 1];
    bool title_truncated;
    bool duration_known;
    uint32_t duration_seconds;
    bool created_at_known;
    uint64_t created_at;
    bool has_media_url;
    bool payment_required_without_media;
    bool media_requires_https_validation;
    char media_url[LIZHI_URL_BYTES + 1];
    bool access_permission_known;
    int32_t access_permission;
    bool access_type_known;
    int32_t access_type;
} lizhi_episode_detail_t;

/* 输入不要求 NUL 终止；失败清空 out；不返回 HTML 正文、token 或邻集猜测。 */
lizhi_parse_result_t lizhi_parse_detail(const char *body, size_t length,
                                      const char *expected_episode_id,
                                      lizhi_episode_detail_t *out);

typedef struct {
    size_t received_count;
    size_t count;
    size_t duplicates;
    lizhi_episode_detail_t items[LIZHI_PAGE_MAX_ITEMS];
} lizhi_episode_page_t;

/* 当前公开列表响应；只去重当前页，不推断总数、下一页或账号关注关系。 */
lizhi_parse_result_t lizhi_parse_program_page(const char *body, size_t length,
                                            const char *expected_author_id,
                                            lizhi_episode_page_t *out);

/* 仅生成同主机同路径的 HTTPS 待验证候选；不得直接视为可播放地址。 */
lizhi_parse_result_t lizhi_https_candidate(const char *source, char *out, size_t capacity);
