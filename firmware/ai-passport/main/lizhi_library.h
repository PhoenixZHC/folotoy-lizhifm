#pragma once
#include "lizhi_app_logic.h"
#define LIZHI_LIBRARY_MAX_AUTHORS 128
typedef struct {
    uint32_t magic, version, count, checksum;
    char owner[LIZHI_ID_BYTES+1];
    lizhi_author_t authors[];
} lizhi_library_t;
size_t lizhi_library_bytes(size_t count);
void lizhi_library_seal(lizhi_library_t *library);
bool lizhi_library_valid(const lizhi_library_t *library,size_t length,const char *owner);
bool lizhi_library_view(const lizhi_library_t *library,uint32_t page,lizhi_follow_view_t *out);
#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t lizhi_library_load(const char *owner,uint32_t page,lizhi_follow_view_t *out,size_t *total);
lizhi_client_result_t lizhi_library_sync(const char *owner,lizhi_cancel_fn cancel,void *context);
#endif
