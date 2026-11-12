#pragma once
#include "lizhi_detail.h"

typedef enum {
    LIZHI_CLIENT_OK = 0,
    LIZHI_CLIENT_INVALID_ARGUMENT,
    LIZHI_CLIENT_NO_MEMORY,
    LIZHI_CLIENT_CANCELLED,
    LIZHI_CLIENT_NETWORK_ERROR,
    LIZHI_CLIENT_NEEDS_AUTH,
    LIZHI_CLIENT_FORBIDDEN,
    LIZHI_CLIENT_RATE_LIMITED,
    LIZHI_CLIENT_REDIRECT_REJECTED,
    LIZHI_CLIENT_HTTP_ERROR,
    LIZHI_CLIENT_BODY_TOO_LARGE,
    LIZHI_CLIENT_INCOMPLETE_BODY,
    LIZHI_CLIENT_UNSUPPORTED_ENCODING,
    LIZHI_CLIENT_FORMAT_ERROR,
    LIZHI_CLIENT_SERVICE_ERROR,
    LIZHI_CLIENT_RANGE_UNSUPPORTED,
    LIZHI_CLIENT_MEDIA_FORMAT_UNSUPPORTED,
    LIZHI_CLIENT_NO_MEDIA_SOURCE,
    LIZHI_CLIENT_PURCHASE_OR_AUTH_REQUIRED,
} lizhi_client_status_t;

typedef bool (*lizhi_cancel_fn)(void *context);
typedef struct {
    lizhi_client_status_t status;
    int http_status;
    int transport_error;
    lizhi_parse_result_t parse_error;
    bool service_code_known;
    int32_t service_code;
    size_t response_bytes;
} lizhi_client_result_t;

/* 由单个 API worker 调用；阻塞请求，不得在按键/LVGL 回调调用。
 * 每次释放 HTTP/TLS；无自动重试、认证、代理回退或 NVS 写入。
 * 取消在连接前、事件边界和请求返回时检查，阻塞读最长受15秒超时约束。 */
lizhi_client_result_t lizhi_client_get_page(const char *author_id, uint32_t page,
                                          lizhi_cancel_fn cancel, void *context,
                                          lizhi_episode_page_t *out);
lizhi_client_result_t lizhi_client_get_detail(const char *episode_id,
                                            lizhi_cancel_fn cancel, void *context,
                                            lizhi_episode_detail_t *out);

typedef struct {
    char https_url[LIZHI_URL_BYTES + 1];
    uint64_t resource_bytes;
} lizhi_media_probe_t;

/* 验证同路径 HTTPS 候选的首4096字节、206及格式线索；不下载整集，不证明可seek。 */
lizhi_client_result_t lizhi_client_probe_media(const char *source_url,
                                             lizhi_cancel_fn cancel, void *context,
                                             lizhi_media_probe_t *out);
