#include "lizhi_app_logic.h"
#include <string.h>
#include <stdlib.h>

void lizhi_browser_init(lizhi_browser_t *s)
{
    memset(s, 0, sizeof(*s));
}

bool lizhi_browser_begin(lizhi_browser_t *s, uint32_t page, uint32_t generation)
{
    if (!page || page > LIZHI_VIEW_MAX_PAGES || !generation) return false;
    s->pending_page = page;
    s->generation = generation;
    s->loading = true;
    return true;
}

bool lizhi_browser_commit(lizhi_browser_t *s, uint32_t generation,
                         const lizhi_row_t *rows, size_t count)
{
    if (!s->loading || generation != s->generation || count > LIZHI_VIEW_MAX_ITEMS ||
        (count && !rows)) return false;
    /* 已有列表向后翻到空页时保留原页，并标记明确的尾部证据。 */
    if (!count && s->valid && s->pending_page > s->page) {
        s->loading = false;
        s->reached_end = true;
        return true;
    }
    memset(s->rows, 0, sizeof(s->rows));
    if (count) memcpy(s->rows, rows, count * sizeof(*rows));
    s->page = s->pending_page;
    s->count = count;
    s->selected = 0;
    s->valid = true;
    s->loading = false;
    /* 短页不是终页证据，只有空页才确认。 */
    s->reached_end = count == 0;
    return true;
}

bool lizhi_browser_fail(lizhi_browser_t *s, uint32_t generation)
{
    if (!s->loading || generation != s->generation) return false;
    s->loading = false;
    return true;
}

void lizhi_browser_cancel(lizhi_browser_t *s)
{
    s->loading = false;
}

void lizhi_browser_move(lizhi_browser_t *s, int direction)
{
    if (s->loading || !s->valid || !s->count) return;
    if (direction > 0 && s->selected + 1 < s->count) ++s->selected;
    if (direction < 0 && s->selected > 0) --s->selected;
}

uint32_t lizhi_browser_page_target(const lizhi_browser_t *s, int direction)
{
    if (!s->valid || s->loading) return 0;
    if (direction < 0) return s->page > 1 ? s->page - 1 : 0;
    if (direction > 0) return !s->reached_end && s->page < LIZHI_VIEW_MAX_PAGES ? s->page + 1 : 0;
    return 0;
}

bool lizhi_follows_begin(lizhi_follow_browser_t *s, uint32_t page, uint32_t generation)
{
    if (!s || !generation || !page || page > LIZHI_FOLLOW_MAX_PAGES ||
        (page > 1 && (!s->cursors[page-1][0] || page > s->number + 1))) return false;
    s->pending = page; s->generation = generation; s->loading = true;
    return true;
}

void lizhi_follows_cancel(lizhi_follow_browser_t *s) { s->loading = false; }

bool lizhi_follows_commit(lizhi_follow_browser_t *s, uint32_t generation,
                          const lizhi_follow_view_t *page)
{
    if (!s || !page || !s->loading || generation != s->generation) return false;
    s->loading = false;
    if (page->count > LIZHI_VIEW_MAX_ITEMS || !memchr(page->cursor, 0, sizeof(page->cursor)) ||
        (!page->last && (!page->count || !page->cursor[0]))) return false;
    if (!page->last) {
        for (uint32_t i = 0; i < s->pending; ++i)
            if (!strcmp(s->cursors[i], page->cursor)) return false;
    }
    uint64_t ids[LIZHI_VIEW_MAX_ITEMS] = {0};
    for (size_t j = 0; j < page->count; ++j) {
        const char *id = page->authors[j].id;
        if (!memchr(id, 0, sizeof(page->authors[j].id)) || !*id || strlen(id) > 19) return false;
        for (const char *p = id; *p; ++p) if (*p < '0' || *p > '9') return false;
        ids[j] = strtoull(id, NULL, 10);
        if (!ids[j] || ids[j] > INT64_MAX) return false;
        for (size_t k = 0; k < j; ++k) if (ids[j] == ids[k]) return false;
        for (uint32_t i = 0; i + 1 < s->pending; ++i)
            for (size_t k = 0; k < LIZHI_VIEW_MAX_ITEMS; ++k)
                if (ids[j] == s->ids[i][k]) return false;
    }
    if (!page->count && page->last && s->number && s->pending == s->number+1) {
        s->page.last = true; return true;
    }
    bool backwards = s->pending < s->number;
    s->page = *page; s->number = s->pending;
    s->selected = backwards && page->count ? page->count-1 : 0;
    memcpy(s->ids[s->number-1], ids, sizeof(ids));
    /* 刷新成功后丢弃旧的后续游标与ID，失败则保留原页。 */
    for (uint32_t i = s->number; i < LIZHI_FOLLOW_MAX_PAGES; ++i) {
        memset(s->ids[i], 0, sizeof(s->ids[i]));
        memset(s->cursors[i], 0, sizeof(s->cursors[i]));
    }
    if (!page->last && s->number < LIZHI_FOLLOW_MAX_PAGES)
        strcpy(s->cursors[s->number], page->cursor);
    return true;
}
