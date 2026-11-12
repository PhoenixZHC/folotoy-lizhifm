#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lizhi_detail.h"
#include "lizhi_follow.h"

/* 屏幕容量独立于接口每次两条的小分页，沿用音乐播放器的四行布局。 */
#define LIZHI_VIEW_MAX_ITEMS 4
#define LIZHI_VIEW_MAX_PAGES (10000 / (LIZHI_VIEW_MAX_ITEMS / LIZHI_PAGE_MAX_ITEMS))
typedef struct {
    size_t count;
    lizhi_author_t authors[LIZHI_VIEW_MAX_ITEMS];
    char cursor[LIZHI_FOLLOW_CURSOR_BYTES + 1];
    bool last;
} lizhi_follow_view_t;

/* UI只持有紧凑元数据，不缓存临时媒体URL。仅由应用控制任务访问。 */
typedef struct {
    char id[LIZHI_ID_BYTES + 1];
    char title[LIZHI_TITLE_BYTES + 1];
    bool duration_known;
    uint32_t seconds;
    bool created_known;
    uint64_t created;
} lizhi_row_t;

typedef struct {
    uint32_t page;
    uint32_t pending_page;
    uint32_t generation;
    bool loading;
    bool valid;
    bool reached_end;
    size_t count;
    size_t selected; /* 仅节目；上下在页边缘触发翻页，与网易云项目一致。 */
    lizhi_row_t rows[LIZHI_VIEW_MAX_ITEMS];
} lizhi_browser_t;

void lizhi_browser_init(lizhi_browser_t *state);
bool lizhi_browser_begin(lizhi_browser_t *state, uint32_t page, uint32_t generation);
bool lizhi_browser_commit(lizhi_browser_t *state, uint32_t generation,
                         const lizhi_row_t *rows, size_t count);
bool lizhi_browser_fail(lizhi_browser_t *state, uint32_t generation);
void lizhi_browser_cancel(lizhi_browser_t *state);
void lizhi_browser_move(lizhi_browser_t *state, int direction);
/* 返回0表示当前边界禁止翻页。失败/取消不推进已确认页号。 */
uint32_t lizhi_browser_page_target(const lizhi_browser_t *state, int direction);

#define LIZHI_FOLLOW_MAX_PAGES 32
typedef struct {
    lizhi_follow_view_t page;
    uint32_t number, pending, generation;
    size_t selected;
    bool loading;
    char cursors[LIZHI_FOLLOW_MAX_PAGES][LIZHI_FOLLOW_CURSOR_BYTES + 1];
    uint64_t ids[LIZHI_FOLLOW_MAX_PAGES][LIZHI_VIEW_MAX_ITEMS];
} lizhi_follow_browser_t;

bool lizhi_follows_begin(lizhi_follow_browser_t *state, uint32_t page, uint32_t generation);
bool lizhi_follows_commit(lizhi_follow_browser_t *state, uint32_t generation,
                          const lizhi_follow_view_t *page);
void lizhi_follows_cancel(lizhi_follow_browser_t *state);
