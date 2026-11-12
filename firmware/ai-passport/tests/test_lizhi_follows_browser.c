#include "lizhi_app_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned passed;
#define CHECK(x) do { assert(x); ++passed; } while (0)
int main(void)
{
    lizhi_follow_browser_t s = {0};
    lizhi_follow_view_t a = {.count = 2, .cursor = "2",
        .authors = {{.id = "11", .name = "A"}, {.id = "12", .name = "B"}}};
    lizhi_follow_view_t b = {.count = 1, .cursor = "3",
        .authors = {{.id = "13", .name = "C"}}};
    CHECK(!lizhi_follows_begin(&s, 0, 1));
    CHECK(!lizhi_follows_begin(&s, 33, 1));
    CHECK(!lizhi_follows_begin(&s, 2, 1));
    CHECK(!lizhi_follows_begin(&s, 1, 0));
    CHECK(lizhi_follows_begin(&s, 1, 1));
    CHECK(!lizhi_follows_commit(&s, 2, &a) && s.loading && !s.number);
    CHECK(lizhi_follows_commit(&s, 1, &a) && s.number == 1 && s.page.count == 2);
    CHECK(lizhi_follows_begin(&s, 2, 2));
    lizhi_follows_cancel(&s);
    CHECK(!lizhi_follows_commit(&s, 2, &b) && s.number == 1);
    CHECK(lizhi_follows_begin(&s, 2, 3));
    CHECK(!lizhi_follows_commit(&s, 3, &a) && s.number == 1 && s.page.count == 2);
    CHECK(lizhi_follows_begin(&s, 2, 4));
    lizhi_follow_view_t duplicate = b;
    strcpy(duplicate.authors[0].id, "12");
    CHECK(!lizhi_follows_commit(&s, 4, &duplicate) && s.number == 1);
    CHECK(lizhi_follows_begin(&s, 2, 5));
    CHECK(lizhi_follows_commit(&s, 5, &b) && s.number == 2 && s.selected == 0);
    CHECK(lizhi_follows_begin(&s, 3, 6));
    lizhi_follow_view_t end = {.last = true};
    CHECK(lizhi_follows_commit(&s, 6, &end) && s.number == 2 && s.page.last && s.page.count == 1);
    CHECK(lizhi_follows_begin(&s, 1, 7));
    CHECK(lizhi_follows_commit(&s, 7, &a) && s.selected == 1 && !s.page.last);
    CHECK(s.cursors[1][0] && !s.cursors[2][0] && !s.ids[1][0]);
    CHECK(lizhi_follows_begin(&s, 1, 8));
    CHECK(lizhi_follows_commit(&s, 8, &end) && s.page.count == 0 && s.number == 1);
    CHECK(!s.cursors[1][0] && !s.ids[0][0]);
    lizhi_follow_view_t four = {.count=4, .cursor="4",
        .authors={{.id="21",.name="A"},{.id="22",.name="B"},{.id="23",.name="C"},{.id="24",.name="D"}}};
    CHECK(lizhi_follows_begin(&s, 1, 9));
    CHECK(lizhi_follows_commit(&s, 9, &four) && s.page.count == 4);
    CHECK(lizhi_follows_begin(&s, 2, 10));
    duplicate = b; strcpy(duplicate.authors[0].id,"24");
    CHECK(!lizhi_follows_commit(&s, 10, &duplicate) && s.page.count == 4);
    CHECK(lizhi_follows_begin(&s, 2, 11));
    CHECK(lizhi_follows_commit(&s, 11, &b));
    CHECK(lizhi_follows_begin(&s, 1, 12));
    CHECK(lizhi_follows_commit(&s, 12, &four) && s.selected == 3);
    printf("关注分页：%u通过/0失败\n", passed);
}
