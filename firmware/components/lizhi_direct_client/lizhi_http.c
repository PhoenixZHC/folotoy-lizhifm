#include "lizhi_client.h"
#include "lizhi_follow.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"

#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    char *body;
    size_t length;
    size_t capacity;
    bool json_type, binary_type;
    bool media, range_valid;
    uint64_t range_end, resource_bytes;
    lizhi_client_status_t error;
    lizhi_cancel_fn cancel;
    void *cancel_context;
} response_t;

static void read_range(response_t *response, const char *value)
{
    response->range_valid = false;
    if (strncmp(value, "bytes 0-", 8) != 0 || value[8] < '0' || value[8] > '9') return;
    char *end;
    errno = 0;
    unsigned long long last = strtoull(value + 8, &end, 10);
    if (errno || *end != '/' || end[1] < '0' || end[1] > '9') return;
    unsigned long long total = strtoull(end + 1, &end, 10);
    if (errno || *end || last >= 4096 || total <= last) return;
    response->range_end = last;
    response->resource_bytes = total;
    response->range_valid = true;
}

static bool valid_id(const char *id)
{
    if (!id) return false;
    size_t i = 0;
    bool nonzero = false;
    for (; i <= LIZHI_ID_BYTES && id[i]; ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
        nonzero |= id[i] != '0';
    }
    return i && i <= LIZHI_ID_BYTES && nonzero;
}

static bool cancelled(response_t *response)
{
    return response->cancel && response->cancel(response->cancel_context);
}

static esp_err_t on_event(esp_http_client_event_t *event)
{
    response_t *response = event->user_data;
    if (!response) return ESP_FAIL;
    if (cancelled(response)) response->error = LIZHI_CLIENT_CANCELLED;
    if (response->error != LIZHI_CLIENT_OK) return ESP_FAIL;
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value) {
        if (response->media && strcasecmp(event->header_key, "Content-Range") == 0)
            read_range(response, event->header_value);
        if (strcasecmp(event->header_key, "Content-Encoding") == 0
            && strcasecmp(event->header_value, "identity") != 0)
            response->error = LIZHI_CLIENT_UNSUPPORTED_ENCODING;
        if (strcasecmp(event->header_key, "Content-Type") == 0) {
            const char *value = event->header_value;
            response->json_type = strncasecmp(value, "application/json", 16) == 0
                && (value[16] == 0 || value[16] == ';' || value[16] == ' ');
            response->binary_type = strncasecmp(value, "application/octet-stream", 24) == 0
                && (value[24] == 0 || value[24] == ';' || value[24] == ' ');
        }
    } else if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t length = (size_t)event->data_len;
        if (!event->data) response->error = LIZHI_CLIENT_FORMAT_ERROR;
        else if (length > response->capacity - response->length)
            response->error = LIZHI_CLIENT_BODY_TOO_LARGE;
        else {
            memcpy(response->body + response->length, event->data, length);
            response->length += length;
            response->body[response->length] = 0;
        }
    }
    return response->error == LIZHI_CLIENT_OK ? ESP_OK : ESP_FAIL;
}

static lizhi_client_status_t http_status(int status)
{
    if (status == 200) return LIZHI_CLIENT_OK;
    if (status == 401) return LIZHI_CLIENT_NEEDS_AUTH;
    if (status == 403) return LIZHI_CLIENT_FORBIDDEN;
    if (status == 429) return LIZHI_CLIENT_RATE_LIMITED;
    if (status >= 300 && status < 400) return LIZHI_CLIENT_REDIRECT_REJECTED;
    return LIZHI_CLIENT_HTTP_ERROR;
}

static lizhi_client_result_t request(const char *url, lizhi_cancel_fn cancel, void *context,
                                     char **out, size_t *length, bool media, uint64_t *resource_bytes,
                                     const unsigned char *post, size_t post_length)
{
    lizhi_client_result_t result = {0};
    response_t response = {.cancel = cancel, .cancel_context = context,
        .media = media, .capacity = media ? 4096 : LIZHI_RESPONSE_MAX_BYTES};
    *out = NULL;
    *length = 0;
    if (cancelled(&response)) { result.status = LIZHI_CLIENT_CANCELLED; return result; }
    response.body = calloc(1, response.capacity + 1);
    if (!response.body) { result.status = LIZHI_CLIENT_NO_MEMORY; return result; }
    esp_http_client_config_t config = {
        .url = url, .method = post ? HTTP_METHOD_POST : HTTP_METHOD_GET, .timeout_ms = 15000,
        .buffer_size = 2048, .buffer_size_tx = 512,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .disable_auto_redirect = true, .event_handler = on_event, .user_data = &response,
#if CONFIG_MBEDTLS_DYNAMIC_BUFFER && CONFIG_ESP_TLS_DYN_BUF_STRATEGY_SUPPORTED
        .tls_dyn_buf_strategy = HTTP_TLS_DYN_BUF_RX_STATIC,
#endif
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) { free(response.body); result.status = LIZHI_CLIENT_NO_MEMORY; return result; }
    esp_err_t error = esp_http_client_set_header(client, "Accept", post ? "application/octet-stream" : media ? "audio/mpeg" : "application/json");
    if (error == ESP_OK) error = esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (error == ESP_OK && media) error = esp_http_client_set_header(client, "Range", "bytes=0-4095");
    if (error == ESP_OK && post) error = esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    if (error == ESP_OK && post) error = esp_http_client_set_header(client, "version", "5");
    if (error == ESP_OK && post) error = esp_http_client_set_header(client, "index", "0");
    if (error == ESP_OK && post) error = esp_http_client_set_post_field(client, (const char *)post, post_length);
    if (error == ESP_OK) error = esp_http_client_perform(client);
    result.transport_error = error;
    result.response_bytes = response.length;
    result.http_status = esp_http_client_get_status_code(client);
    int64_t declared = esp_http_client_get_content_length(client);
    if (cancelled(&response) || response.error == LIZHI_CLIENT_CANCELLED)
        result.status = LIZHI_CLIENT_CANCELLED;
    else if (media && result.http_status == 200)
        result.status = LIZHI_CLIENT_RANGE_UNSUPPORTED;
    else if (result.http_status > 0 && result.http_status != (media ? 206 : 200))
        result.status = http_status(result.http_status);
    else if (response.error != LIZHI_CLIENT_OK) result.status = response.error;
    else if (error != ESP_OK) result.status = LIZHI_CLIENT_NETWORK_ERROR;
    else if (result.http_status <= 0) result.status = LIZHI_CLIENT_NETWORK_ERROR;
    else if (declared > (int64_t)response.capacity) result.status = LIZHI_CLIENT_BODY_TOO_LARGE;
    else if (!esp_http_client_is_complete_data_received(client)
             || (declared >= 0 && (uint64_t)declared != response.length))
        result.status = LIZHI_CLIENT_INCOMPLETE_BODY;
    else if (post ? !response.binary_type : (!media && !response.json_type)) result.status = LIZHI_CLIENT_FORMAT_ERROR;
    else if (media && (!response.range_valid || response.range_end + 1 != response.length))
        result.status = LIZHI_CLIENT_RANGE_UNSUPPORTED;
    esp_http_client_cleanup(client);
    if (result.status == LIZHI_CLIENT_OK) {
        *out = response.body;
        *length = response.length;
        if (resource_bytes) *resource_bytes = response.resource_bytes;
    } else free(response.body);
    return result;
}

static void parsed_result(lizhi_client_result_t *result, lizhi_parse_result_t status, const char *body)
{
    result->parse_error = status;
    switch (status) {
    case LIZHI_PARSE_OK: result->status = LIZHI_CLIENT_OK; break;
    case LIZHI_PARSE_NO_MEMORY: result->status = LIZHI_CLIENT_NO_MEMORY; break;
    case LIZHI_PARSE_TOO_LARGE: result->status = LIZHI_CLIENT_BODY_TOO_LARGE; break;
    case LIZHI_PARSE_SERVICE_ERROR: {
        result->status = LIZHI_CLIENT_SERVICE_ERROR;
        /* 只在已验证业务错误时读取数值码；不返回服务端原文、token 或签名 URL。 */
        cJSON *root = cJSON_Parse(body);
        const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
        if (cJSON_IsNumber(code) && code->valuedouble >= INT32_MIN
            && code->valuedouble <= INT32_MAX && floor(code->valuedouble) == code->valuedouble) {
            result->service_code_known = true;
            result->service_code = (int32_t)code->valuedouble;
        }
        cJSON_Delete(root);
        break;
    }
    default: result->status = LIZHI_CLIENT_FORMAT_ERROR; break;
    }
}

lizhi_client_result_t lizhi_client_get_page(const char *author_id, uint32_t page,
                                          lizhi_cancel_fn cancel, void *context,
                                          lizhi_episode_page_t *out)
{
    lizhi_client_result_t result = {.status = LIZHI_CLIENT_INVALID_ARGUMENT};
    if (!out) return result;
    memset(out, 0, sizeof(*out));
    if (!valid_id(author_id) || page < 1 || page > 10000) return result;
    char url[160];
    snprintf(url, sizeof(url), "https://m.lizhi.fm/vodapi/user/%s?pageNo=%lu&pageSize=%u",
             author_id, (unsigned long)page, LIZHI_PAGE_MAX_ITEMS);
    char *body = NULL;
    size_t length = 0;
    result = request(url, cancel, context, &body, &length, false, NULL, NULL, 0);
    if (result.status == LIZHI_CLIENT_OK) {
        parsed_result(&result, lizhi_parse_program_page(body, length, author_id, out), body);
        free(body);
    }
    return result;
}

lizhi_client_result_t lizhi_client_get_detail(const char *episode_id,
                                            lizhi_cancel_fn cancel, void *context,
                                            lizhi_episode_detail_t *out)
{
    lizhi_client_result_t result = {.status = LIZHI_CLIENT_INVALID_ARGUMENT};
    if (!out) return result;
    memset(out, 0, sizeof(*out));
    if (!valid_id(episode_id)) return result;
    char url[128];
    snprintf(url, sizeof(url), "https://m.lizhi.fm/vodapi/voice/info/%s", episode_id);
    char *body = NULL;
    size_t length = 0;
    result = request(url, cancel, context, &body, &length, false, NULL, NULL, 0);
    if (result.status == LIZHI_CLIENT_OK) {
        parsed_result(&result, lizhi_parse_detail(body, length, episode_id, out), body);
        free(body);
    }
    return result;
}

lizhi_client_result_t lizhi_client_probe_media(const char *source_url,
                                             lizhi_cancel_fn cancel, void *context,
                                             lizhi_media_probe_t *out)
{
    lizhi_client_result_t result = {.status = LIZHI_CLIENT_INVALID_ARGUMENT};
    if (!out) return result;
    memset(out, 0, sizeof(*out));
    char *candidate = malloc(LIZHI_URL_BYTES + 1);
    if (!candidate) { result.status = LIZHI_CLIENT_NO_MEMORY; return result; }
    if (lizhi_https_candidate(source_url, candidate, LIZHI_URL_BYTES + 1) != LIZHI_PARSE_OK) {
        free(candidate);
        return result;
    }
    char *body = NULL;
    size_t length = 0;
    uint64_t total = 0;
    result = request(candidate, cancel, context, &body, &length, true, &total, NULL, 0);
    if (result.status == LIZHI_CLIENT_OK) {
        const unsigned char *bytes = (const unsigned char *)body;
        bool id3 = length >= 3 && memcmp(body, "ID3", 3) == 0;
        bool frame = length >= 4 && bytes[0] == 255 && (bytes[1] & 0xe0) == 0xe0
            && (bytes[1] & 0x18) != 8 && (bytes[1] & 6) != 0
            && (bytes[2] & 0xf0) != 0 && (bytes[2] & 0xf0) != 0xf0 && (bytes[2] & 12) != 12;
        if (!id3 && !frame) result.status = LIZHI_CLIENT_MEDIA_FORMAT_UNSUPPORTED;
        else {
            strcpy(out->https_url, candidate);
            out->resource_bytes = total;
        }
    }
    free(body);
    free(candidate);
    return result;
}

lizhi_client_result_t lizhi_client_get_follows(const char *owner, const char *cursor,
                                              lizhi_cancel_fn cancel, void *context,
                                              lizhi_follow_page_t *out)
{
    lizhi_client_result_t result = {.status = LIZHI_CLIENT_INVALID_ARGUMENT};
    if (!out) return result;
    memset(out, 0, sizeof(*out));
    unsigned char post[128];
    size_t post_length = lizhi_follow_encode(owner, cursor, post, sizeof(post));
    if (!post_length) return result;
    char *body; size_t length;
    result = request("https://httpproxy101.lizhifm.com/op/5132", cancel, context,
                     &body, &length, false, NULL, post, post_length);
    if (result.status == LIZHI_CLIENT_OK) {
        int32_t code;
        result.status = lizhi_follow_parse((const unsigned char *)body, length, owner, out, &code);
        result.service_code_known = code != INT32_MIN;
        result.service_code = code;
        if (result.status == LIZHI_CLIENT_OK && !out->last && !strcmp(cursor, out->cursor)) {
            memset(out, 0, sizeof(*out)); result.status = LIZHI_CLIENT_FORMAT_ERROR;
        }
        free(body);
    }
    return result;
}
