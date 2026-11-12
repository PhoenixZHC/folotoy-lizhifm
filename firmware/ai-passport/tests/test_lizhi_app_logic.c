#include "lizhi_app_logic.h"
#include <assert.h>
static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
#include <stdio.h>
#include <string.h>

int main(void)
{
    lizhi_browser_t s;
    lizhi_browser_init(&s);
    lizhi_row_t rows[2] = {{.id="123", .title="真实节目甲"}, {.id="456", .title="真实节目乙"}};
    CHECK(!lizhi_browser_page_target(&s, 1));
    CHECK(!lizhi_browser_begin(&s, 0, 1));
    CHECK(!lizhi_browser_begin(&s, 10001, 1));
    CHECK(lizhi_browser_begin(&s, 1, 2));
    CHECK(!lizhi_browser_commit(&s, 1, rows, 2));
    CHECK(s.loading && !s.valid);
    CHECK(lizhi_browser_commit(&s, 2, rows, 2));
    CHECK(s.page == 1 && s.count == 2 && !s.loading);
    CHECK(!lizhi_browser_page_target(&s, -1));
    CHECK(lizhi_browser_page_target(&s, 1) == 2);
    lizhi_browser_move(&s, -1); CHECK(s.selected == 0);
    lizhi_browser_move(&s, 1); CHECK(s.selected == 1);
    lizhi_browser_move(&s, 1); CHECK(s.selected == 1);
    lizhi_browser_move(&s, -1); CHECK(s.selected == 0);
    CHECK(lizhi_browser_begin(&s, 2, 3));
    lizhi_browser_move(&s, 1); CHECK(s.selected == 0);
    CHECK(!lizhi_browser_fail(&s, 2));
    CHECK(lizhi_browser_fail(&s, 3));
    CHECK(s.page == 1 && s.count == 2 && !strcmp(s.rows[0].id,"123"));
    CHECK(lizhi_browser_begin(&s, 2, 4));
    lizhi_browser_cancel(&s);
    CHECK(!lizhi_browser_commit(&s, 4, rows, 2));
    CHECK(lizhi_browser_begin(&s, 2, 5));
    CHECK(lizhi_browser_commit(&s, 5, rows, 1));
    CHECK(s.page == 2 && !s.reached_end && s.count == 1);
    CHECK(lizhi_browser_begin(&s, 3, 6));
    CHECK(lizhi_browser_commit(&s, 6, NULL, 0));
    CHECK(s.page == 2 && s.reached_end && s.count == 1);
    CHECK(!lizhi_browser_page_target(&s, 1));
    CHECK(lizhi_browser_page_target(&s, -1) == 1);
    CHECK(lizhi_browser_begin(&s, 1, 7));
    CHECK(lizhi_browser_commit(&s, 7, rows, 2));
    CHECK(!s.reached_end && s.page == 1);
    CHECK(lizhi_browser_begin(&s, 1, 8));
    CHECK(!lizhi_browser_commit(&s, 8, rows, LIZHI_VIEW_MAX_ITEMS+1));
    CHECK(!lizhi_browser_commit(&s, 8, NULL, 1));
    CHECK(lizhi_browser_fail(&s, 8));
    lizhi_browser_init(&s);
    CHECK(lizhi_browser_begin(&s, 1, 9));
    CHECK(lizhi_browser_commit(&s, 9, NULL, 0));
    CHECK(s.valid && s.reached_end && !s.count && s.page == 1);
    lizhi_row_t four[4] = {{.id="1"},{.id="2"},{.id="3"},{.id="4"}};
    CHECK(lizhi_browser_begin(&s, 1, 10));
    CHECK(lizhi_browser_commit(&s, 10, four, 4));
    for (int i = 0; i < 4; ++i) lizhi_browser_move(&s, 1);
    CHECK(s.selected == 3 && !strcmp(s.rows[3].id, "4"));
    CHECK(lizhi_browser_page_target(&s, 1) == 2);
    CHECK(!lizhi_browser_begin(&s, LIZHI_VIEW_MAX_PAGES+1, 11));
    printf("lizhi_app_logic: %u checks passed\n", checks);
}
