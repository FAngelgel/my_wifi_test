#include <esp_log.h>

#include "wifi.h"

#define TAG "main"

extern "C" void app_main(void)
{
    if (!wifi_app_start())
    {
        ESP_LOGE(TAG, "wifi_app_start failed");
    }
}
