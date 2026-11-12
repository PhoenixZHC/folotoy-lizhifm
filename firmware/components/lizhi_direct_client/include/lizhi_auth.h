#pragma once
#include "lizhi_client.h"

#define LIZHI_AUTH_TOKEN_BYTES 512
#define LIZHI_AUTH_QR_BYTES 512
typedef struct {
    char ticket[LIZHI_AUTH_TOKEN_BYTES+1];
    char url[LIZHI_AUTH_QR_BYTES+1];
    uint32_t valid_seconds;
} lizhi_auth_qr_t;
typedef struct {
    char id[LIZHI_ID_BYTES+1];
    char name[LIZHI_TITLE_BYTES+1];
} lizhi_account_t;
typedef enum { LIZHI_AUTH_WAITING, LIZHI_AUTH_EXPIRED, LIZHI_AUTH_CONFIRMED } lizhi_auth_state_t;

lizhi_client_status_t lizhi_auth_parse_qr(const char *body, size_t length, lizhi_auth_qr_t *out);
lizhi_client_status_t lizhi_auth_parse_poll(const char *body, size_t length,
    lizhi_auth_state_t *state, char *token, size_t capacity);
lizhi_client_status_t lizhi_auth_parse_account(const char *body, size_t length, lizhi_account_t *out);
lizhi_client_result_t lizhi_auth_create(lizhi_cancel_fn cancel, void *context, lizhi_auth_qr_t *out);
lizhi_client_result_t lizhi_auth_poll(const char *ticket, lizhi_cancel_fn cancel, void *context,
    lizhi_auth_state_t *state, lizhi_account_t *out);

lizhi_client_status_t lizhi_auth_parse_login(const char *body,size_t length);
