set(probe_sources "probe_main.c")
list(APPEND probe_sources "../../../firmware/ai-passport/main/lizhi_app_logic.c")
list(APPEND probe_sources "../../../firmware/ai-passport/main/lizhi_list_loader.c")
if(CONFIG_LIZHI_PROBE_POSITION)
    list(APPEND probe_sources "../../../firmware/ai-passport/main/lizhi_progress.c")
endif()
idf_component_register(SRCS ${probe_sources} INCLUDE_DIRS "." "../../../firmware/ai-passport/main"
    REQUIRES lizhi_direct_client audio_stream esp_wifi esp_event esp_netif nvs_flash
             esp_timer lwip esp_system)
