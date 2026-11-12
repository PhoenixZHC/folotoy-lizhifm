#include "lizhi_app.h"
#include "bsp_battery.h"
#include "esp_log.h"
void app_main(void)
{
    esp_err_t error=bsp_battery_init();
    if(error!=ESP_OK)ESP_LOGW("lizhi_main","电量计初始化失败：%s",esp_err_to_name(error));
    lizhi_app_run();
}
