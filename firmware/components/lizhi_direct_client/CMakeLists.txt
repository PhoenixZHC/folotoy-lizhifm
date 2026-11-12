# 公开节目与普通关注关系查询；不存储账号凭据。
idf_component_register(SRCS "lizhi_detail.c" "lizhi_follow.c" "lizhi_http.c" "lizhi_auth.c" "lizhi_auth_http.c" INCLUDE_DIRS "include"
                       REQUIRES json esp_http_client mbedtls)
