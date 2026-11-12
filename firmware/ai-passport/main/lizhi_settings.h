#pragma once
#include "lizhi_auth.h"

typedef struct {
    uint32_t magic, version, generation;
    lizhi_account_t account;
    bool bound;
    uint8_t volume, brightness;
    uint16_t timeout_seconds;
    uint32_t checksum;
} lizhi_settings_t;
void lizhi_settings_defaults(lizhi_settings_t *settings);
void lizhi_settings_seal(lizhi_settings_t *settings);
bool lizhi_settings_valid(const lizhi_settings_t *settings);
bool lizhi_settings_bind(lizhi_settings_t *settings, const lizhi_account_t *account);
void lizhi_settings_unbind(lizhi_settings_t *settings);
#ifdef ESP_PLATFORM
#include "esp_err.h"
esp_err_t lizhi_settings_load(lizhi_settings_t *settings);
esp_err_t lizhi_settings_save(lizhi_settings_t *settings);
#endif
