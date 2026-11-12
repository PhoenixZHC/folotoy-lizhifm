#pragma once
#include "lizhi_client.h"

#define LIZHI_FOLLOW_CURSOR_BYTES 64
typedef struct {
    char id[LIZHI_ID_BYTES + 1];
    char name[LIZHI_TITLE_BYTES + 1];
} lizhi_author_t;
typedef struct {
    size_t count;
    lizhi_author_t authors[LIZHI_PAGE_MAX_ITEMS];
    char cursor[LIZHI_FOLLOW_CURSOR_BYTES + 1];
    bool last;
} lizhi_follow_page_t;

/* 普通关注 relation=1；不请求直播订阅，不携带手机凭据或设备指纹。 */
size_t lizhi_follow_encode(const char *owner, const char *cursor, unsigned char *out, size_t capacity);
lizhi_client_status_t lizhi_follow_parse(const unsigned char *body, size_t length,
                                        const char *owner, lizhi_follow_page_t *out, int32_t *code);
lizhi_client_result_t lizhi_client_get_follows(const char *owner, const char *cursor,
                                              lizhi_cancel_fn cancel, void *context,
                                              lizhi_follow_page_t *out);
