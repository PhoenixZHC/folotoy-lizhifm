#pragma once
#include "lizhi_app_logic.h"
#include "audio_position.h"

/* 保留原本地记录格式；账号记录另有包含账号ID的校验封套。 */
typedef struct {
    uint32_t magic, version;
    char scope[16];
    char author[LIZHI_ID_BYTES + 1];
    lizhi_row_t episode;
    uint32_t position_ms;
    audio_checkpoint_t checkpoint;
    uint32_t checksum;
} lizhi_progress_t;

void lizhi_progress_seal(lizhi_progress_t *record);
bool lizhi_progress_valid(const lizhi_progress_t *record, const char *author);
typedef struct {
    char owner[LIZHI_ID_BYTES+1];
    lizhi_progress_t record;
    uint32_t checksum;
} lizhi_scoped_progress_t;
void lizhi_scoped_progress_seal(lizhi_scoped_progress_t *record);
bool lizhi_scoped_progress_valid(const lizhi_scoped_progress_t *record,const char *owner);

#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t lizhi_progress_load(lizhi_progress_t *record, const char *author);
esp_err_t lizhi_progress_save(lizhi_progress_t *record, const char *author);
void lizhi_progress_set_owner(const char *owner);
#define LIZHI_RECENT_COUNT 4
typedef struct {
    uint32_t magic, version, count;
    char owner[LIZHI_ID_BYTES+1];
    lizhi_progress_t records[LIZHI_RECENT_COUNT];
    uint32_t checksum;
} lizhi_history_t;
esp_err_t lizhi_history_load(lizhi_history_t *history);
#endif
