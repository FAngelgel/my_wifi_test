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

// If the device stays disconnected for a long time (e.g. moved to a new place, router changed),
// automatically start the config AP so the user can enter new credentials.
//
// Note: We intentionally do NOT start config AP immediately on disconnect because temporary
// outages (router reboot / hotspot sleep) should recover automatically.
constexpr int64_t kConfigApAfterDisconnectedUs = 3LL * 60LL * 1000LL * 1000LL; // 3 minutes

void wifi_monitor_task(void * /*arg*/)
{
    auto &wifi = WifiManager::GetInstance();
    int64_t last_seen_connected_us = esp_timer_get_time();
    bool config_ap_forced = false;

    while (true)
    {
        if (wifi.IsConnected())
        {
            ESP_LOGI(TAG, "Status: CONNECTED | IP: %s | RSSI: %d dBm",
                     wifi.GetIpAddress().c_str(),
                     wifi.GetRssi());
            last_seen_connected_us = esp_timer_get_time();
            config_ap_forced = false;
        }
        else if (wifi.IsConfigMode())
        {
            ESP_LOGW(TAG, "Status: CONFIG MODE (AP) | SSID: %s | URL: %s",
                     wifi.GetApSsid().c_str(),
                     wifi.GetApWebUrl().c_str());
        }
        else
        {
            ESP_LOGI(TAG, "Status: DISCONNECTED / SEARCHING...");

            // If we've been disconnected for too long, fall back to config AP to let the user
            // update credentials (useful when the saved SSID is no longer available).
            const int64_t now_us = esp_timer_get_time();
            if (!config_ap_forced && (now_us - last_seen_connected_us) > kConfigApAfterDisconnectedUs)
            {
                ESP_LOGW(TAG, "Disconnected for too long, starting config AP");
                wifi.StartConfigAp();
                config_ap_forced = true;
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
    // Faster auto-reconnect after AP/router power cycles.
    config.station_scan_min_interval_seconds = 5;
    config.station_scan_max_interval_seconds = 60;
    // Keep AP off while STA starts. This avoids AP+STA scan/connect interference and
    // guarantees the station connect flow is clean. If STA can't connect, it will
    // keep scanning/retrying in the background (no need to re-config for temporary outages).
    config.keep_ap_on_station_start = false;
    // Don't automatically switch into config AP when Wi-Fi temporarily goes down.
    // Keep the station running so it can reconnect when the AP comes back.
    config.restart_ap_on_disconnect = false;

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
