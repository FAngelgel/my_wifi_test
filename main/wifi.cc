#include "wifi.h"

#include <esp_err.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "ssid_manager.h"
#include "wifi_manager.h"

namespace {

constexpr const char *TAG = "wifi_app";

void wifi_monitor_task(void * /*arg*/)
{
    auto &wifi = WifiManager::GetInstance();

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

        ESP_LOGD(TAG, "Free Heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t init_nvs()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

} // namespace

bool wifi_app_start()
{
    // 1) Initialize the default event loop (ignore if already created)
    esp_err_t ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
        return false;
    }

    // 2) Initialize NVS flash
    ret = init_nvs();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    // 3) Setup and Start Wi-Fi Manager
    auto &wifi = WifiManager::GetInstance();

    WifiManagerConfig config;
    config.ssid_prefix = "Jingqi_S3_Setup";
    config.language = "en-US";
    // Keep AP off while STA starts. This avoids AP+STA scan/connect interference and
    // guarantees the station connect flow is clean. If STA fails, our monitor will
    // bring the config AP back automatically.
    config.keep_ap_on_station_start = false;
    config.restart_ap_on_disconnect = true;

    if (!wifi.Initialize(config))
    {
        ESP_LOGE(TAG, "WifiManager Failed to Initialize");
        return false;
    }

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
        // Improve long HTTPS transfers (OTA) reliability/throughput.
        // Disables modem sleep power-save while connected.
        wifi.SetPowerSaveLevel(WifiPowerSaveLevel::PERFORMANCE);
    }

    // 4) Monitoring task
    static constexpr uint32_t kMonitorStack = 4096;
    static constexpr UBaseType_t kMonitorPrio = 5;
    if (xTaskCreate(&wifi_monitor_task, "wifi_mon", kMonitorStack, nullptr, kMonitorPrio, nullptr) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create wifi monitor task");
        return false;
    }

    return true;
}
