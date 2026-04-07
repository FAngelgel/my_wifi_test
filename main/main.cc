#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>

#include "led.h"
#include "mqtt_app.h"
#include "ota_update.h"
#include "wifi.h"

#define TAG "main"

namespace
{

    constexpr const char *kFirmwareUrl =
        "https://gitee.com/cutekawaigirl/my_wifi_test/releases/download/EEG_detect/EEG_detect.bin";
    constexpr const char *kVersionUrl =
        "https://gitee.com/cutekawaigirl/my_wifi_test/releases/download/EEG_detect/version.txt";

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
        options.timeout_ms = 60000;
        options.max_redirects = 10;
        options.user_agent = "ESP32S3-EEG-Device";

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
    // OTA rollback is enabled in sdkconfig. After a successful OTA update the new app boots in
    // PENDING_VERIFY state and must mark itself valid, otherwise the bootloader will roll back
    // to the previous firmware on the next reboot/power cycle.
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        ESP_LOGW(TAG, "App is pending verify; marking as valid to cancel rollback");
        const esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_ota_mark_app_valid_cancel_rollback failed: %s", esp_err_to_name(ret));
        }
    }

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
        mqtt_app_start();
    }

    if (!wifi_app_start())
    {
        ESP_LOGE(TAG, "wifi_app_start failed");
    }
}
