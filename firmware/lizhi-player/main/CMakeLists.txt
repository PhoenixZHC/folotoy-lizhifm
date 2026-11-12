set(app "${CMAKE_CURRENT_LIST_DIR}/../../ai-passport/main")
idf_component_register(
    SRCS "main.c" "${app}/lizhi_app.c" "${app}/lizhi_app_logic.c"
         "${app}/lizhi_direct_adapter.c" "${app}/lizhi_list_loader.c" "${app}/lizhi_progress.c"
         "${app}/lizhi_settings.c" "${app}/lizhi_library.c" "${app}/wifi_provisioning_logic.c"
         "${app}/fonts/netease_ui_font_14.c"
         "${app}/device_music_ui.c" "${app}/device_music_ui_spec.c" "${app}/display_policy.c"
         "${app}/fonts/netease_ui_font_8.c" "${app}/fonts/netease_ui_font_9.c"
         "${app}/fonts/netease_ui_font_10.c" "${app}/fonts/netease_ui_font_12.c"
         "${app}/fonts/netease_ui_font_13.c" "${app}/fonts/netease_ui_font_17.c"
         "${app}/fonts/netease_ui_font_18.c" "${app}/fonts/netease_ui_font_22.c"
         "${app}/fonts/netease_ui_font_24.c" "${app}/fonts/netease_ui_font_43.c"
         "${app}/fonts/netease_ui_font_46.c" "${app}/fonts/netease_ui_font_60.c"
         "${app}/fonts/netease_ui_font_80.c"
    INCLUDE_DIRS "${app}"
    REQUIRES audio_stream bsp lizhi_direct_client esp_event esp_netif esp_wifi
             esp_timer nvs_flash lwip esp_driver_usb_serial_jtag esp_app_format
)
