#pragma once
#include "lizhi_app_logic.h"

/* 两次串行小请求组成一屏；任一次失败均不发布半屏数据。 */
lizhi_client_result_t lizhi_load_follow_view(const char *owner, const char *cursor,
    lizhi_cancel_fn cancel, void *context, lizhi_follow_view_t *out);
lizhi_client_result_t lizhi_load_program_view(const char *author, uint32_t page,
    lizhi_cancel_fn cancel, void *context, lizhi_row_t *rows, size_t *count);
