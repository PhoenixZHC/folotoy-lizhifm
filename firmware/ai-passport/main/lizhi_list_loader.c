#include "lizhi_list_loader.h"
#include <stdlib.h>
#include <string.h>

_Static_assert(LIZHI_VIEW_MAX_ITEMS % LIZHI_PAGE_MAX_ITEMS == 0, "完整小页组成一屏");

lizhi_client_result_t lizhi_load_follow_view(const char *owner, const char *cursor,
    lizhi_cancel_fn cancel, void *context, lizhi_follow_view_t *out)
{
    lizhi_client_result_t result = {.status = LIZHI_CLIENT_INVALID_ARGUMENT};
    if (!out) return result;
    memset(out, 0, sizeof(*out));
    if (!cursor || strlen(cursor) > LIZHI_FOLLOW_CURSOR_BYTES) return result;
    lizhi_follow_view_t view = {0};
    char next[LIZHI_FOLLOW_CURSOR_BYTES+1];
    strcpy(next, cursor);
    for (size_t part = 0; part < LIZHI_VIEW_MAX_ITEMS/LIZHI_PAGE_MAX_ITEMS; ++part) {
        if (cancel && cancel(context)) { result.status = LIZHI_CLIENT_CANCELLED; return result; }
        lizhi_follow_page_t small;
        result = lizhi_client_get_follows(owner, next, cancel, context, &small);
        if (result.status != LIZHI_CLIENT_OK) return result;
        if (small.count > LIZHI_PAGE_MAX_ITEMS || view.count + small.count > LIZHI_VIEW_MAX_ITEMS ||
            (!small.last && (!small.count || !small.cursor[0] || !strcmp(next, small.cursor) || !strcmp(cursor, small.cursor)))) {
            result.status = LIZHI_CLIENT_FORMAT_ERROR; return result;
        }
        for (size_t i = 0; i < small.count; ++i) {
            for (size_t j = 0; j < view.count; ++j)
                if (!strcmp(small.authors[i].id, view.authors[j].id)) {
                    result.status = LIZHI_CLIENT_FORMAT_ERROR; return result;
                }
            view.authors[view.count++] = small.authors[i];
        }
        strcpy(next, small.cursor);
        strcpy(view.cursor, next); view.last = small.last;
        if (view.last) break;
    }
    if (cancel && cancel(context)) { result.status = LIZHI_CLIENT_CANCELLED; return result; }
    *out = view;
    return result;
}

lizhi_client_result_t lizhi_load_program_view(const char *author, uint32_t page,
    lizhi_cancel_fn cancel, void *context, lizhi_row_t *rows, size_t *count)
{
    lizhi_client_result_t result = {.status = LIZHI_CLIENT_INVALID_ARGUMENT};
    if (!rows || !count) return result;
    memset(rows, 0, sizeof(*rows)*LIZHI_VIEW_MAX_ITEMS); *count = 0;
    if (!page || page > LIZHI_VIEW_MAX_PAGES) return result;
    lizhi_episode_page_t *small = calloc(1, sizeof(*small));
    if (!small) { result.status = LIZHI_CLIENT_NO_MEMORY; return result; }
    size_t n = 0;
    for (uint32_t part = 0; part < LIZHI_VIEW_MAX_ITEMS/LIZHI_PAGE_MAX_ITEMS; ++part) {
        if (cancel && cancel(context)) { result.status = LIZHI_CLIENT_CANCELLED; break; }
        uint32_t api_page = (page-1)*(LIZHI_VIEW_MAX_ITEMS/LIZHI_PAGE_MAX_ITEMS)+part+1;
        result = lizhi_client_get_page(author, api_page, cancel, context, small);
        if (result.status != LIZHI_CLIENT_OK) break;
        if (small->count > LIZHI_PAGE_MAX_ITEMS || n+small->count > LIZHI_VIEW_MAX_ITEMS) {
            result.status = LIZHI_CLIENT_FORMAT_ERROR; break;
        }
        if (!small->count) break; /* 只有实际空页才是节目末尾证据，短页继续。 */
        for (size_t i = 0; i < small->count; ++i) {
            const lizhi_episode_detail_t *detail = &small->items[i];
            for (size_t j = 0; j < n; ++j) {
                if (!strcmp(rows[j].id, detail->episode_id)) {
                    result.status = LIZHI_CLIENT_FORMAT_ERROR; break;
                }
            }
            if (result.status != LIZHI_CLIENT_OK) break;
            lizhi_row_t *row = &rows[n++];
            memcpy(row->id, detail->episode_id, sizeof(row->id));
            memcpy(row->title, detail->title, sizeof(row->title));
            row->seconds = detail->duration_seconds; row->duration_known = detail->duration_known;
            row->created = detail->created_at; row->created_known = detail->created_at_known;
        }
        if (result.status != LIZHI_CLIENT_OK) break;
    }
    free(small);
    if (cancel && cancel(context)) result.status = LIZHI_CLIENT_CANCELLED;
    if (result.status == LIZHI_CLIENT_OK) *count = n;
    else memset(rows, 0, sizeof(*rows)*LIZHI_VIEW_MAX_ITEMS);
    return result;
}
