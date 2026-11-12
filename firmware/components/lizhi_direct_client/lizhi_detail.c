#include "lizhi_detail.h"
#include "cJSON.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool id_valid(const char *id)
{
    if (!id) return false;
    size_t i = 0;
    bool nonzero = false;
    for (; i <= LIZHI_ID_BYTES && id[i]; ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
        nonzero |= id[i] != '0';
    }
    return i > 0 && i <= LIZHI_ID_BYTES && nonzero;
}

static bool utf8_valid(const unsigned char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        unsigned c = s[i++], extra, minimum;
        if (c < 128) { if (!c) return false; else continue; }
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; minimum = 0x80; c &= 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; minimum = 0x800; c &= 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; minimum = 0x10000; c &= 7; }
        else return false;
        if (extra > n - i) return false;
        for (unsigned j = 0; j < extra; ++j) {
            if ((s[i] & 0xc0) != 0x80) return false;
            c = (c << 6) | (s[i++] & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
    }
    return true;
}

/* cJSON 之前限制深度/节点数，拒绝内嵌 NUL 与宽松数字语法，避免截断及过量分配。 */
static bool preflight(const char *s, size_t n)
{
    if (!utf8_valid((const unsigned char *)s, n)) return false;
    unsigned depth = 0, tokens = 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"') {
            if (++tokens > 512) return false;
            bool closed = false;
            while (++i < n) {
                if ((unsigned char)s[i] < 32) return false;
                if (s[i] == '"') { closed = true; break; }
                if (s[i] == '\\') {
                    if (++i >= n) return false;
                    if (s[i] == 'u' && i + 4 < n && memcmp(s + i + 1, "0000", 4) == 0) return false;
                }
            }
            if (!closed) return false;
        } else if (c == '{' || c == '[') {
            if (++depth > 24 || ++tokens > 512) return false;
        } else if (c == '}' || c == ']') {
            if (!depth) return false;
            --depth;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            if (++tokens > 512) return false;
            size_t j = i;
            if (s[j] == '-' && ++j == n) return false;
            if (s[j] == '0') ++j;
            else {
                if (s[j] < '1' || s[j] > '9') return false;
                while (j < n && s[j] >= '0' && s[j] <= '9') ++j;
            }
            if (j < n && s[j] == '.') {
                size_t start = ++j;
                while (j < n && s[j] >= '0' && s[j] <= '9') ++j;
                if (j == start) return false;
            }
            if (j < n && (s[j] == 'e' || s[j] == 'E')) {
                ++j;
                if (j < n && (s[j] == '+' || s[j] == '-')) ++j;
                size_t start = j;
                while (j < n && s[j] >= '0' && s[j] <= '9') ++j;
                if (j == start) return false;
            }
            if (j < n && !strchr(" \t\r\n,]}", s[j])) return false;
            i = j - 1;
        } else if (c == 't' || c == 'f' || c == 'n') {
            if (++tokens > 512) return false;
        } else if (c < 32 && c != '\n' && c != '\r' && c != '\t') return false;
    }
    return depth == 0;
}

static bool unique_keys(const cJSON *value)
{
    for (const cJSON *a = value->child; a; a = a->next) {
        if (cJSON_IsObject(value)) {
            for (const cJSON *b = a->next; b; b = b->next)
                if (a->string && b->string && strcmp(a->string, b->string) == 0) return false;
        }
        if (!unique_keys(a)) return false;
    }
    return true;
}

static const cJSON *field(const cJSON *object, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(object, key);
}

static bool integer(const cJSON *value, double minimum, double maximum)
{
    return cJSON_IsNumber(value) && isfinite(value->valuedouble)
        && value->valuedouble >= minimum && value->valuedouble <= maximum
        && floor(value->valuedouble) == value->valuedouble;
}

static bool copy_id(const cJSON *value, char *out)
{
    if (!cJSON_IsString(value) || !id_valid(value->valuestring)) return false;
    strcpy(out, value->valuestring);
    return true;
}

static bool media_url_valid(const char *url, bool *plain_http)
{
    size_t length = strlen(url);
    if (length > LIZHI_URL_BYTES) return false;
    const char *host;
    if (strncmp(url, "https://", 8) == 0) { *plain_http = false; host = url + 8; }
    else if (strncmp(url, "http://", 7) == 0) { *plain_http = true; host = url + 7; }
    else return false;
    const char *end = host + strcspn(host, "/?#");
    if (end == host) return false;
    for (const char *p = host; p < end; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
              || (c >= '0' && c <= '9') || c == '.' || c == '-')) return false;
    }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 32 || c >= 127 || c == '\\' || c == '#') return false;
    }
    return true;
}

lizhi_parse_result_t lizhi_https_candidate(const char *source, char *out, size_t capacity)
{
    if (!out || !capacity) return LIZHI_PARSE_INVALID_ARGUMENT;
    out[0] = 0;
    bool plain = false;
    if (!source || !media_url_valid(source, &plain)) return LIZHI_PARSE_INVALID_URL;
    size_t length = strlen(source) + (plain ? 1 : 0);
    if (length > LIZHI_URL_BYTES || length >= capacity) return LIZHI_PARSE_INVALID_URL;
    if (plain) {
        memcpy(out, "https://", 8);
        memcpy(out + 8, source + 7, strlen(source + 7) + 1);
    } else memcpy(out, source, length + 1);
    return LIZHI_PARSE_OK;
}

static lizhi_parse_result_t project_voice(const cJSON *voice, const char *expected,
                                   lizhi_episode_detail_t *out)
{
    const cJSON *info = field(voice, "voiceInfo");
    const cJSON *user = field(voice, "userInfo");
    if (!cJSON_IsObject(voice) || !cJSON_IsObject(info)
        || !cJSON_IsObject(user)) return LIZHI_PARSE_SCHEMA_CHANGED;
    if (!copy_id(field(info, "voiceId"), out->episode_id)
        || (expected && strcmp(out->episode_id, expected) != 0)
        || !copy_id(field(user, "userId"), out->author_id)) return LIZHI_PARSE_SCHEMA_CHANGED;
    const cJSON *jockey = field(info, "jockeyId");
    if (jockey && (!cJSON_IsString(jockey) || strcmp(jockey->valuestring, out->author_id) != 0))
        return LIZHI_PARSE_SCHEMA_CHANGED;
    const cJSON *name = field(info, "name");
    if (!cJSON_IsString(name) || !name->valuestring[0]) return LIZHI_PARSE_SCHEMA_CHANGED;
    size_t length = strlen(name->valuestring), count = length;
    if (count > LIZHI_TITLE_BYTES) {
        count = LIZHI_TITLE_BYTES;
        while (((unsigned char)name->valuestring[count] & 0xc0) == 0x80) --count;
        out->title_truncated = true;
    }
    memcpy(out->title, name->valuestring, count);
    out->title[count] = 0;
    const cJSON *duration = field(info, "duration");
    if (duration && !cJSON_IsNull(duration)) {
        if (!integer(duration, 0, UINT32_MAX)) return LIZHI_PARSE_SCHEMA_CHANGED;
        out->duration_known = true;
        out->duration_seconds = (uint32_t)duration->valuedouble;
    }
    const cJSON *created = field(info, "createTime");
    if (created && !cJSON_IsNull(created)) {
        if (!integer(created, 0, 9007199254740991.0)) return LIZHI_PARSE_SCHEMA_CHANGED;
        out->created_at_known = true;
        out->created_at = (uint64_t)created->valuedouble;
    }
    const cJSON *play = field(voice, "voicePlayProperty");
    if (!play) return LIZHI_PARSE_OK;
    if (!cJSON_IsObject(play)) return LIZHI_PARSE_SCHEMA_CHANGED;
    const cJSON *access = field(play, "playAccessProperty");
    if (access) {
        if (!cJSON_IsObject(access)) return LIZHI_PARSE_SCHEMA_CHANGED;
        const cJSON *permission = field(access, "accessPermission"), *type = field(access, "accessType");
        if (permission) {
            if (!integer(permission, INT32_MIN, INT32_MAX)) return LIZHI_PARSE_SCHEMA_CHANGED;
            out->access_permission_known = true;
            out->access_permission = (int32_t)permission->valuedouble;
        }
        if (type) {
            if (!integer(type, INT32_MIN, INT32_MAX)) return LIZHI_PARSE_SCHEMA_CHANGED;
            out->access_type_known = true;
            out->access_type = (int32_t)type->valuedouble;
        }
    }
    const cJSON *track = field(play, "trackUrl");
    if (!track || cJSON_IsNull(track)) return LIZHI_PARSE_OK;
    if (!cJSON_IsString(track)) return LIZHI_PARSE_SCHEMA_CHANGED;
    if (!track->valuestring[0]) return LIZHI_PARSE_OK;
    if (!media_url_valid(track->valuestring, &out->media_requires_https_validation))
        return LIZHI_PARSE_INVALID_URL;
    strcpy(out->media_url, track->valuestring);
    out->has_media_url = true;
    return LIZHI_PARSE_OK;
}

static lizhi_parse_result_t decode_response(const char *body, size_t length, cJSON **out)
{
    *out = NULL;
    if (!body || !length) return LIZHI_PARSE_INVALID_ARGUMENT;
    if (length > LIZHI_RESPONSE_MAX_BYTES) return LIZHI_PARSE_TOO_LARGE;
    if (!preflight(body, length)) return LIZHI_PARSE_INVALID_JSON;
    char *copy = malloc(length + 1);
    if (!copy) return LIZHI_PARSE_NO_MEMORY;
    memcpy(copy, body, length);
    copy[length] = 0;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(copy, length + 1, &end, true);
    lizhi_parse_result_t status = root ? LIZHI_PARSE_INVALID_JSON : LIZHI_PARSE_JSON_FAILED;
    if (root && cJSON_IsObject(root) && unique_keys(root)) {
        const cJSON *code = field(root, "code");
        if (!integer(code, INT32_MIN, INT32_MAX)) status = LIZHI_PARSE_SCHEMA_CHANGED;
        else if (code->valuedouble != 0) status = LIZHI_PARSE_SERVICE_ERROR;
        else status = LIZHI_PARSE_OK;
    }
    free(copy);
    if (status == LIZHI_PARSE_OK) *out = root;
    else cJSON_Delete(root);
    return status;
}

lizhi_parse_result_t lizhi_parse_detail(const char *body, size_t length,
                                      const char *expected_episode_id,
                                      lizhi_episode_detail_t *out)
{
    if (!out) return LIZHI_PARSE_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!id_valid(expected_episode_id)) return LIZHI_PARSE_INVALID_ARGUMENT;
    cJSON *root = NULL;
    lizhi_parse_result_t status = decode_response(body, length, &root);
    if (status != LIZHI_PARSE_OK) return status;
    const cJSON *data = field(root, "data");
    lizhi_episode_detail_t result = {0};
    status = cJSON_IsObject(data) ? project_voice(field(data, "userVoice"), expected_episode_id, &result)
                                 : LIZHI_PARSE_SCHEMA_CHANGED;
    const cJSON *price=field(field(data,"payProperty"),"price");
    if(status==LIZHI_PARSE_OK&&!result.has_media_url&&cJSON_IsNumber(price)&&
        isfinite(price->valuedouble)&&price->valuedouble>0&&
        cJSON_IsFalse(field(field(data,"userVoice"),"userHadBuy")))
        result.payment_required_without_media=true;
    if (status == LIZHI_PARSE_OK) *out = result;
    cJSON_Delete(root);
    return status;
}

lizhi_parse_result_t lizhi_parse_program_page(const char *body, size_t length,
                                            const char *expected_author_id,
                                            lizhi_episode_page_t *out)
{
    if (!out) return LIZHI_PARSE_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));
    if (!id_valid(expected_author_id)) return LIZHI_PARSE_INVALID_ARGUMENT;
    cJSON *root = NULL;
    lizhi_parse_result_t status = decode_response(body, length, &root);
    if (status != LIZHI_PARSE_OK) return status;
    const cJSON *rows = field(root, "data");
    if (!cJSON_IsArray(rows)) status = LIZHI_PARSE_SCHEMA_CHANGED;
    else if (cJSON_GetArraySize(rows) > LIZHI_PAGE_MAX_ITEMS) status = LIZHI_PARSE_TOO_LARGE;
    if (status != LIZHI_PARSE_OK) { cJSON_Delete(root); return status; }
    /* 避免在 API worker 栈上再放整页元数据；失败不发布部分列表。 */
    lizhi_episode_page_t *page = calloc(1, sizeof(*page));
    if (!page) { cJSON_Delete(root); return LIZHI_PARSE_NO_MEMORY; }
    const cJSON *row;
    cJSON_ArrayForEach(row, rows) {
        lizhi_episode_detail_t *item = &page->items[page->count];
        status = project_voice(row, NULL, item);
        if (status != LIZHI_PARSE_OK) break;
        if (strcmp(item->author_id, expected_author_id) != 0) {
            status = LIZHI_PARSE_SCHEMA_CHANGED;
            break;
        }
        ++page->received_count;
        bool duplicate = false;
        for (size_t i = 0; i < page->count; ++i)
            if (strcmp(page->items[i].episode_id, item->episode_id) == 0) duplicate = true;
        if (duplicate) {
            ++page->duplicates;
            memset(item, 0, sizeof(*item));
        } else ++page->count;
    }
    if (status == LIZHI_PARSE_OK) *out = *page;
    free(page);
    cJSON_Delete(root);
    return status;
}
