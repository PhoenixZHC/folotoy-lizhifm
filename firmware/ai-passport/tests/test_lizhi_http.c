#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lizhi_client.h"
#include "lizhi_follow.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static struct fake_http { esp_http_client_config_t config; } http;
static int status, perform_error, init_count, cleanup_count, header_error;
static bool complete, fail_init, cancelled, cancel_during;
static const char *body, *encoding, *type;
static int64_t declared_length;
static const char *content_range;
static char requested_url[256];
static unsigned passed;
static size_t binary_length;
static bool follow_request;
static int post_error;
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t h, const char *data, int length)
{
    (void)h;
    unsigned char expected[128];
    size_t n = lizhi_follow_encode("123", "", expected, sizeof(expected));
    assert(follow_request && length == (int)n && !memcmp(data, expected, n));
    return post_error;
}

esp_err_t esp_crt_bundle_attach(void *p) { (void)p; return ESP_OK; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config)
{
    ++init_count;
    if (fail_init) return NULL;
    assert(config->method == (follow_request ? HTTP_METHOD_POST : HTTP_METHOD_GET) && config->disable_auto_redirect);
    assert(config->crt_bundle_attach == esp_crt_bundle_attach);
    assert(config->timeout_ms == 15000);
    snprintf(requested_url, sizeof(requested_url), "%s", config->url);
    http.config = *config;
    return &http;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t h, const char *key, const char *value)
{
    (void)h;
    assert((strcmp(key, "Accept") == 0 && (strcmp(value, "application/json") == 0 || strcmp(value, "audio/mpeg") == 0 || (follow_request && !strcmp(value, "application/octet-stream"))))
        || (follow_request && !strcmp(key, "Content-Type") && !strcmp(value, "application/octet-stream"))
        || (follow_request && !strcmp(key, "version") && !strcmp(value, "5"))
        || (follow_request && !strcmp(key, "index") && !strcmp(value, "0"))
        || (strcmp(key, "Range") == 0 && strcmp(value, "bytes=0-4095") == 0)
        || (strcmp(key, "Accept-Encoding") == 0 && strcmp(value, "identity") == 0));
    return header_error;
}
static int emit(int kind, const char *key, const char *value, int size)
{
    esp_http_client_event_t event = {.event_id = kind, .user_data = http.config.user_data,
        .header_key = key, .header_value = value, .data = value, .data_len = size};
    return http.config.event_handler(&event);
}
esp_err_t esp_http_client_perform(esp_http_client_handle_t h)
{
    (void)h;
    if (perform_error) return perform_error;
    int error = emit(HTTP_EVENT_ON_HEADER, "Content-Type", type, 0);
    if (!error && encoding) error = emit(HTTP_EVENT_ON_HEADER, "Content-Encoding", encoding, 0);
    if (!error && content_range) error = emit(HTTP_EVENT_ON_HEADER, "Content-Range", content_range, 0);
    if (error) return error;
    if (cancel_during) cancelled = true;
    size_t length = follow_request ? binary_length : strlen(body);
    for (size_t offset = 0; offset < length; offset += 37) {
        size_t n = length - offset < 37 ? length - offset : 37;
        error = emit(HTTP_EVENT_ON_DATA, NULL, body + offset, (int)n);
        if (error) return error;
    }
    return ESP_OK;
}
int esp_http_client_get_status_code(esp_http_client_handle_t h) { (void)h; return status; }
int64_t esp_http_client_get_content_length(esp_http_client_handle_t h) { (void)h; return declared_length; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t h) { (void)h; return complete; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t h) { (void)h; ++cleanup_count; return ESP_OK; }
static bool is_cancelled(void *p) { (void)p; return cancelled; }
static void reset(void)
{
    status = 200; perform_error = header_error = 0;
    complete = true; fail_init = cancelled = cancel_during = false;
    body = "{\"code\":0,\"data\":[]}"; encoding = NULL; type = "application/json;charset=UTF-8";
    declared_length = -1;
    init_count = cleanup_count = 0;
    content_range = NULL;
    follow_request = false; binary_length = 0; post_error = 0;
}
static void follow_check(lizhi_client_status_t expected)
{
    lizhi_follow_page_t out;
    memset(&out, 0xa5, sizeof(out));
    lizhi_client_result_t result = lizhi_client_get_follows("123", "", is_cancelled, NULL, &out);
    assert(result.status == expected);
    assert(cleanup_count == init_count);
    if (expected != LIZHI_CLIENT_OK) {
        lizhi_follow_page_t empty = {0}; assert(!memcmp(&out, &empty, sizeof(out)));
    }
    ++passed;
}
static void follow_reset(void)
{
    static const unsigned char empty[] = {16,0,48,1};
    reset(); follow_request = true; type = "application/octet-stream";
    body = (const char *)empty; binary_length = sizeof(empty);
}
static void check(lizhi_client_status_t expected)
{
    lizhi_episode_page_t out;
    memset(&out, 0x7f, sizeof(out));
    lizhi_client_result_t result = lizhi_client_get_page("5036275272706175532", 2, is_cancelled, NULL, &out);
    assert(result.status == expected);
    assert(cleanup_count == (fail_init || (cancelled && init_count == 0) ? 0 : init_count));
    if (expected != LIZHI_CLIENT_OK) {
        lizhi_episode_page_t empty = {0};
        assert(memcmp(&out, &empty, sizeof(out)) == 0);
    }
    ++passed;
}
int main(void)
{
    reset(); check(LIZHI_CLIENT_OK);
    assert(strcmp(requested_url, "https://m.lizhi.fm/vodapi/user/5036275272706175532?pageNo=2&pageSize=2") == 0);
    reset(); declared_length = (int64_t)strlen(body); check(LIZHI_CLIENT_OK);
    reset(); status = 401; check(LIZHI_CLIENT_NEEDS_AUTH);
    reset(); status = 403; check(LIZHI_CLIENT_FORBIDDEN);
    reset(); status = 429; check(LIZHI_CLIENT_RATE_LIMITED);
    reset(); status = 302; check(LIZHI_CLIENT_REDIRECT_REJECTED);
    reset(); status = 500; check(LIZHI_CLIENT_HTTP_ERROR);
    reset(); status = 0; check(LIZHI_CLIENT_NETWORK_ERROR);
    reset(); perform_error = ESP_ERR_TIMEOUT; check(LIZHI_CLIENT_NETWORK_ERROR);
    reset(); header_error = ESP_FAIL; check(LIZHI_CLIENT_NETWORK_ERROR);
    reset(); fail_init = true; check(LIZHI_CLIENT_NO_MEMORY);
    reset(); cancelled = true; check(LIZHI_CLIENT_CANCELLED); assert(init_count == 0);
    reset(); cancel_during = true; check(LIZHI_CLIENT_CANCELLED);
    reset(); complete = false; check(LIZHI_CLIENT_INCOMPLETE_BODY);
    reset(); declared_length = 999; check(LIZHI_CLIENT_INCOMPLETE_BODY);
    reset(); declared_length = LIZHI_RESPONSE_MAX_BYTES + 1; check(LIZHI_CLIENT_BODY_TOO_LARGE);
    reset(); encoding = "gzip"; check(LIZHI_CLIENT_UNSUPPORTED_ENCODING);
    reset(); type = "text/html"; check(LIZHI_CLIENT_FORMAT_ERROR);
    reset(); body = "{\"code\":0,\"data\":[null]}"; check(LIZHI_CLIENT_FORMAT_ERROR);
    reset(); body = "{\"code\":12345,\"data\":[]}"; check(LIZHI_CLIENT_SERVICE_ERROR);
    lizhi_episode_page_t page;
    lizhi_client_result_t result = lizhi_client_get_page("5036275272706175532", 1, NULL, NULL, &page);
    assert(result.service_code_known && result.service_code == 12345 && result.http_status == 200);
    ++passed;
    reset(); status = 403; result = lizhi_client_get_page("5036275272706175532", 1, NULL, NULL, &page);
    assert(result.http_status == 403);
    ++passed;
    reset(); char large[LIZHI_RESPONSE_MAX_BYTES + 2]; memset(large, ' ', sizeof(large) - 1); large[sizeof(large) - 1] = 0;
    body = large; check(LIZHI_CLIENT_BODY_TOO_LARGE);
    reset(); result = lizhi_client_get_page("1/../../", 1, NULL, NULL, &page);
    assert(result.status == LIZHI_CLIENT_INVALID_ARGUMENT && init_count == 0); ++passed;
    result = lizhi_client_get_page("5036275272706175532", 0, NULL, NULL, &page);
    assert(result.status == LIZHI_CLIENT_INVALID_ARGUMENT && init_count == 0); ++passed;
    result = lizhi_client_get_page("5036275272706175532", 10001, NULL, NULL, &page);
    assert(result.status == LIZHI_CLIENT_INVALID_ARGUMENT && init_count == 0); ++passed;
    lizhi_episode_detail_t detail;
    body = "{\"code\":0,\"data\":{\"userVoice\":{\"userInfo\":{\"userId\":\"5036275272706175532\"},"
           "\"voiceInfo\":{\"voiceId\":\"3243638952740496390\",\"name\":\"节目\"}}}}";
    result = lizhi_client_get_detail("3243638952740496390", NULL, NULL, &detail);
    assert(result.status == LIZHI_CLIENT_OK && strcmp(detail.title, "节目") == 0);
    assert(strcmp(requested_url, "https://m.lizhi.fm/vodapi/voice/info/3243638952740496390") == 0); ++passed;
    lizhi_media_probe_t media;
    reset(); status = 206; type = "audio/mpeg"; body = "ID3x"; content_range = "bytes 0-3/40964473";
    result = lizhi_client_probe_media("http://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_OK && media.resource_bytes == 40964473);
    assert(strcmp(media.https_url, "https://cdn5.lizhi.fm/a.mp3") == 0 && cleanup_count == 1); ++passed;
    status = 200;
    result = lizhi_client_probe_media("http://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_RANGE_UNSUPPORTED && !media.https_url[0]); ++passed;
    status = 206; content_range = "bytes 1-4/40964473";
    result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_RANGE_UNSUPPORTED && !media.https_url[0]); ++passed;
    content_range = "bytes 0-3/40964473"; body = "HTML";
    result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_MEDIA_FORMAT_UNSUPPORTED); ++passed;
    result = lizhi_client_probe_media("http://user:secret@cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_INVALID_ARGUMENT && !media.https_url[0]); ++passed;
    body = "ID3x";
    const char *bad_ranges[] = {"bytes 0-3/*", "bytes 0-3/2", "bytes 0-3/-1",
        "bytes 0-3/18446744073709551616", "bytes 0-4096/40964473", "bytes 0-3/100x", "bytes 0-2/100"};
    for (size_t i = 0; i < sizeof(bad_ranges) / sizeof(bad_ranges[0]); ++i) {
        content_range = bad_ranges[i];
        result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
        assert(result.status == LIZHI_CLIENT_RANGE_UNSUPPORTED && !media.https_url[0]); ++passed;
    }
    content_range = "bytes 0-3/40964473"; body = "\xff\xfb\x90\x64";
    result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_OK); ++passed;
    body = "\xff\xf1\x50\x80";
    result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_MEDIA_FORMAT_UNSUPPORTED); ++passed;
    body = "ID3x"; status = 403;
    result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", NULL, NULL, &media);
    assert(result.status == LIZHI_CLIENT_FORBIDDEN && result.http_status == 403); ++passed;
    status = 206; cancelled = true;
    result = lizhi_client_probe_media("https://cdn5.lizhi.fm/a.mp3", is_cancelled, NULL, &media);
    assert(result.status == LIZHI_CLIENT_CANCELLED && !media.https_url[0]); ++passed;
    follow_reset(); follow_check(LIZHI_CLIENT_OK);
    assert(!strcmp(requested_url, "https://httpproxy101.lizhifm.com/op/5132"));
    follow_reset(); type = "application/json"; follow_check(LIZHI_CLIENT_FORMAT_ERROR);
    follow_reset(); status = 403; follow_check(LIZHI_CLIENT_FORBIDDEN);
    follow_reset(); status = 302; follow_check(LIZHI_CLIENT_REDIRECT_REJECTED);
    follow_reset(); status = 429; follow_check(LIZHI_CLIENT_RATE_LIMITED);
    follow_reset(); complete = false; follow_check(LIZHI_CLIENT_INCOMPLETE_BODY);
    follow_reset(); encoding = "gzip"; follow_check(LIZHI_CLIENT_UNSUPPORTED_ENCODING);
    follow_reset(); post_error = ESP_FAIL; follow_check(LIZHI_CLIENT_NETWORK_ERROR);
    follow_reset(); cancel_during = true; follow_check(LIZHI_CLIENT_CANCELLED);
    follow_reset(); declared_length = 5; follow_check(LIZHI_CLIENT_INCOMPLETE_BODY);
    follow_reset(); body = "\x10\x01"; binary_length = 2; follow_check(LIZHI_CLIENT_SERVICE_ERROR);
    follow_reset(); binary_length = 1; follow_check(LIZHI_CLIENT_FORMAT_ERROR);
    printf("荔枝 HTTP：%u 通过，0 失败\n", passed);
    return 0;
}
