#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "wifi_manager.h"
#include "ssid_manager.h"

#define TAG "main"

extern "C" void app_main(void)
{
    // 1. Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2. Initialize NVS flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 3. Setup and Start Wi-Fi Manager
    // This will handle the AP and Web Server automatically if no credentials exist
    auto &wifi = WifiManager::GetInstance();

    WifiManagerConfig config;
    config.ssid_prefix = "Jingqi_S3_Setup";
    config.language = "en-US";
    // Keep AP on while station tries to connect, so the phone stays connected.
    config.keep_ap_on_station_start = true;
    // If STA drops after being connected, restart AP for reconfiguration.
    config.restart_ap_on_disconnect = true;

    if (wifi.Initialize(config))
    {
        ESP_LOGI(TAG, "WifiManager Initialized Successfully");

        const auto &ssid_list = SsidManager::GetInstance().GetSsidList();
        if (ssid_list.empty())
        {
            ESP_LOGW(TAG, "No saved SSIDs. Starting config AP.");
            wifi.StartConfigAp();
        }
        else
        {
            wifi.StartStation();
        }
    }
    else
    {
        ESP_LOGE(TAG, "WifiManager Failed to Initialize");
    }

    // 4. Monitoring Loop
    // Instead of launching the old App, we keep the main task alive
    // and log system health periodically.
    int disconnected_seconds = 0;
    while (true)
    {
        if (wifi.IsConnected())
        {
            ESP_LOGI(TAG, "Status: CONNECTED | IP: %s | RSSI: %d dBm",
                     wifi.GetIpAddress().c_str(),
                     wifi.GetRssi());
            disconnected_seconds = 0;
        }
        else if (wifi.IsConfigMode())
        {
            ESP_LOGW(TAG, "Status: CONFIG MODE (AP) | SSID: %s | URL: %s",
                     wifi.GetApSsid().c_str(),
                     wifi.GetApWebUrl().c_str());
            disconnected_seconds = 0;
        }
        else
        {
            ESP_LOGI(TAG, "Status: DISCONNECTED / SEARCHING...");
            disconnected_seconds += 5;
            if (disconnected_seconds >= 30)
            {
                ESP_LOGW(TAG, "Disconnected for %d seconds, starting config AP", disconnected_seconds);
                wifi.StartConfigAp();
                disconnected_seconds = 0;
            }
        }

        // Log free memory to check for leaks during Web Server usage
        ESP_LOGD(TAG, "Free Heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

        vTaskDelay(pdMS_TO_TICKS(5000)); // Log every 5 seconds
    }
}
