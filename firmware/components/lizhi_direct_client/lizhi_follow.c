#include "lizhi_follow.h"
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

typedef struct { const unsigned char *p, *end; } reader_t;
typedef struct { unsigned field, wire; uint64_t value; reader_t bytes; } field_t;

static bool number(const char *s, uint64_t *out)
{
    if (!s || !*s || strlen(s) > 19) return false;
    uint64_t n = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9' || n > (INT64_MAX - (unsigned)(*s-'0'))/10) return false;
        n = n*10 + (unsigned)(*s-'0');
    }
    *out = n; return n != 0;
}

static size_t varint(unsigned char *p, uint64_t n)
{
    size_t i = 0;
    do { p[i++] = (n & 127) | (n > 127 ? 128 : 0); n >>= 7; } while (n);
    return i;
}

size_t lizhi_follow_encode(const char *owner, const char *cursor, unsigned char *out, size_t capacity)
{
    uint64_t uid;
    if (!out || !cursor || !number(owner, &uid)) return 0;
    size_t length = strlen(cursor);
    if (length > LIZHI_FOLLOW_CURSOR_BYTES || capacity < 32 + length) return 0;
    size_t n = 0;
    out[n++] = 10; out[n++] = 0; /* 已实测空 head 可读取公开关注关系。 */
    out[n++] = 16; n += varint(out+n, uid);
    out[n++] = 24; out[n++] = 1;
    out[n++] = 32; out[n++] = LIZHI_PAGE_MAX_ITEMS;
    if (length) { out[n++] = 42; out[n++] = length; memcpy(out+n, cursor, length); n += length; }
    out[n++] = 48; out[n++] = 1;
    return n;
}

static bool read_varint(reader_t *r, uint64_t *value)
{
    *value = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        if (r->p == r->end) return false;
        unsigned char byte = *r->p++;
        if (shift == 63 && byte > 1) return false;
        *value |= (uint64_t)(byte & 127) << shift;
        if (!(byte & 128)) return true;
    }
    return false;
}

static bool field(reader_t *r, field_t *f)
{
    uint64_t tag, n;
    memset(f, 0, sizeof(*f));
    if (!read_varint(r, &tag) || !(tag >> 3) || tag >> 3 > 0x1fffffff) return false;
    f->field = tag >> 3; f->wire = tag & 7;
    if (f->wire == 0) return read_varint(r, &f->value);
    if (f->wire == 2) { if (!read_varint(r, &n)) return false; }
    else if (f->wire == 1 || f->wire == 5) n = f->wire == 1 ? 8 : 4;
    else return false;
    if (n > (uint64_t)(r->end - r->p)) return false;
    f->bytes = (reader_t){r->p, r->p+n}; r->p += n; return true;
}

/* 验证完整 UTF-8 后按字符边界截断昵称；拒绝嵌入 NUL 和控制字符。 */
static bool name_copy(reader_t r, char *out)
{
    size_t kept = 0; bool truncated = false;
    if (r.p == r.end) return false;
    while (r.p < r.end) {
        const unsigned char *start = r.p;
        unsigned c = *r.p++, extra = c < 0x80 ? 0 : c >= 0xc2 && c <= 0xdf ? 1 :
            c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 4;
        if (c < 32 || c == 127 || extra == 4 || (size_t)(r.end-r.p) < extra) return false;
        if (extra && ((c == 0xe0 && r.p[0] < 0xa0) || (c == 0xed && r.p[0] >= 0xa0) ||
            (c == 0xf0 && r.p[0] < 0x90) || (c == 0xf4 && r.p[0] >= 0x90))) return false;
        for (unsigned i = 0; i < extra; ++i) if ((r.p[i] & 0xc0) != 0x80) return false;
        r.p += extra;
        if (!truncated && kept + extra + 1 <= LIZHI_TITLE_BYTES) {
            memcpy(out+kept, start, extra+1); kept += extra+1;
        } else truncated = true;
    }
    out[kept] = 0; return true;
}

static bool user(reader_t r, lizhi_author_t *out)
{
    field_t f; unsigned seen = 0;
    while (r.p < r.end) {
        if (!field(&r, &f)) return false;
        if (f.field == 1) {
            if (f.wire || (seen & 1) || !f.value || f.value > INT64_MAX) return false;
            snprintf(out->id, sizeof(out->id), "%" PRIu64, f.value); seen |= 1;
        } else if (f.field == 2) {
            if (f.wire != 2 || (seen & 2) || !name_copy(f.bytes, out->name)) return false;
            seen |= 2;
        }
    }
    return seen == 3;
}

static bool user_plus(reader_t r, lizhi_author_t *out)
{
    field_t f; bool seen = false;
    while (r.p < r.end) {
        if (!field(&r, &f)) return false;
        if (f.field == 1) {
            if (seen || f.wire != 2 || !user(f.bytes, out)) return false;
            seen = true;
        }
    }
    return seen;
}

lizhi_client_status_t lizhi_follow_parse(const unsigned char *body, size_t length,
                                        const char *owner, lizhi_follow_page_t *out, int32_t *code)
{
    if (!out || !code) return LIZHI_CLIENT_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out)); *code = INT32_MIN;
    uint64_t uid;
    if (!body || !number(owner, &uid)) return LIZHI_CLIENT_INVALID_ARGUMENT;
    if (length > LIZHI_RESPONSE_MAX_BYTES) return LIZHI_CLIENT_BODY_TOO_LARGE;
    lizhi_follow_page_t page = {0};
    reader_t r = {body, body+length}; field_t f;
    bool got_code = false, got_last = false, got_cursor = false;
    while (r.p < r.end) {
        if (!field(&r, &f)) return LIZHI_CLIENT_FORMAT_ERROR;
        if (f.field == 2) {
            if (got_code || f.wire || (f.value > UINT32_MAX && f.value < UINT64_MAX-INT32_MAX))
                return LIZHI_CLIENT_FORMAT_ERROR;
            got_code = true; *code = (int32_t)(uint32_t)f.value;
        } else if (f.field == 3) {
            if (f.wire != 2 || page.count == LIZHI_PAGE_MAX_ITEMS || !user_plus(f.bytes, &page.authors[page.count]))
                return LIZHI_CLIENT_FORMAT_ERROR;
            for (size_t i = 0; i < page.count; ++i)
                if (!strcmp(page.authors[i].id, page.authors[page.count].id)) return LIZHI_CLIENT_FORMAT_ERROR;
            page.count++;
        } else if (f.field == 5) {
            if (f.wire != 2) return LIZHI_CLIENT_FORMAT_ERROR;
            size_t n = f.bytes.end - f.bytes.p;
            if (got_cursor || f.wire != 2 || n > LIZHI_FOLLOW_CURSOR_BYTES || memchr(f.bytes.p, 0, n))
                return LIZHI_CLIENT_FORMAT_ERROR;
            memcpy(page.cursor, f.bytes.p, n); page.cursor[n] = 0; got_cursor = true;
        } else if (f.field == 6) {
            if (got_last || f.wire || f.value > 1) return LIZHI_CLIENT_FORMAT_ERROR;
            page.last = f.value; got_last = true;
        }
    }
    if (!got_code) return LIZHI_CLIENT_FORMAT_ERROR;
    if (*code) return LIZHI_CLIENT_SERVICE_ERROR;
    if (!got_last || (!page.last && (!page.count || !page.cursor[0]))) return LIZHI_CLIENT_FORMAT_ERROR;
    *out = page; return LIZHI_CLIENT_OK;
}
