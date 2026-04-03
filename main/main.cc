#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>

#include "led.h"
#include "ota_update.h"
#include "wifi.h"

#define TAG "main"

namespace {

constexpr const char *kFirmwareUrl =
    "https://github.com/FAngelgel/my_wifi_test/releases/download/EEG_detect/v2.0.3_atk-dnesp32s3.bin";
constexpr const char *kVersionUrl =
    "https://github.com/FAngelgel/my_wifi_test/releases/download/EEG_detect/version.txt";

static bool s_ota_in_progress = false;

void ota_check_task(void * /*arg*/)
{
    if (s_ota_in_progress)
    {
        vTaskDelete(nullptr);
        return;
    }
    s_ota_in_progress = true;

    OtaUpdateOptions options;
    options.timeout_ms = 20000;
    options.max_redirects = 8;

    (void)ota_check_and_update(kVersionUrl, kFirmwareUrl, options);

    s_ota_in_progress = false;
    vTaskDelete(nullptr);
}

void on_got_ip(void * /*arg*/, esp_event_base_t event_base, int32_t event_id, void * /*event_data*/)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        static constexpr uint32_t kOtaStack = 8192;
        static constexpr UBaseType_t kOtaPrio = 5;
        xTaskCreate(&ota_check_task, "ota_check", kOtaStack, nullptr, kOtaPrio, nullptr);
    }
}

} // namespace

extern "C" void app_main(void)
{
    led_init();
    led_start_version_blink();

    // Register IP event handler before starting Wi-Fi.
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, nullptr));
    }

    if (!wifi_app_start())
    {
        ESP_LOGE(TAG, "wifi_app_start failed");
    }
}
