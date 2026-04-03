#include "ota_update.h"

#include <cstring>
#include <string>

#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_netif_sntp.h>

namespace {

constexpr const char *TAG = "ota_update";

constexpr int kMaxVersionTextBytes = 128;

bool sync_time_sntp(const char *server, TickType_t wait_ticks)
{
    if (server == nullptr || server[0] == '\0')
    {
        return false;
    }

    // Init and start SNTP. This uses IP_EVENT_STA_GOT_IP by default in the config macro,
    // but we start it explicitly anyway.
    const esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_sntp_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_netif_sntp_start();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "esp_netif_sntp_start failed: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_netif_sntp_sync_wait(wait_ticks);
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "SNTP sync not finished: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool http_get_text(const char *https_url, const OtaUpdateOptions &options, std::string &out_text)
{
    out_text.clear();

    if (https_url == nullptr || https_url[0] == '\0')
    {
        ESP_LOGE(TAG, "Version URL is empty");
        return false;
    }
    if (std::strncmp(https_url, "https://", 8) != 0)
    {
        ESP_LOGE(TAG, "Version URL must be https:// (got: %s)", https_url);
        return false;
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = https_url;
    http_cfg.timeout_ms = options.timeout_ms;
    http_cfg.keep_alive_enable = true;
    http_cfg.max_redirection_count = options.max_redirects;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "esp_http_client_init failed");
        return false;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_http_client_open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    err = esp_http_client_fetch_headers(client);
    if (err < 0)
    {
        ESP_LOGE(TAG, "esp_http_client_fetch_headers failed: %s", esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }
    const int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Version HTTP status: %d", status);

    char buf[64];
    int total = 0;
    while (true)
    {
        const int read = esp_http_client_read(client, buf, sizeof(buf));
        if (read < 0)
        {
            ESP_LOGE(TAG, "esp_http_client_read failed");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        if (read == 0)
        {
            break;
        }
        if (total + read > kMaxVersionTextBytes)
        {
            ESP_LOGE(TAG, "Version text too large (> %d bytes)", kMaxVersionTextBytes);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return false;
        }
        out_text.append(buf, buf + read);
        total += read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // Trim whitespace
    while (!out_text.empty() && (out_text.back() == '\n' || out_text.back() == '\r' ||
                                 out_text.back() == ' ' || out_text.back() == '\t'))
    {
        out_text.pop_back();
    }
    size_t start = 0;
    while (start < out_text.size() &&
           (out_text[start] == '\n' || out_text[start] == '\r' ||
            out_text[start] == ' ' || out_text[start] == '\t'))
    {
        ++start;
    }
    if (start > 0)
    {
        out_text.erase(0, start);
    }

    return !out_text.empty();
}

bool parse_version(const std::string &text, int &major, int &minor, int &patch)
{
    major = minor = patch = 0;
    if (text.empty())
    {
        return false;
    }

    size_t i = 0;
    if (text[i] == 'v' || text[i] == 'V')
    {
        ++i;
    }

    auto read_number = [&](int &out) -> bool {
        if (i >= text.size() || text[i] < '0' || text[i] > '9')
        {
            return false;
        }
        int value = 0;
        while (i < text.size() && text[i] >= '0' && text[i] <= '9')
        {
            value = value * 10 + (text[i] - '0');
            ++i;
        }
        out = value;
        return true;
    };

    if (!read_number(major))
    {
        return false;
    }
    if (i < text.size() && text[i] == '.')
    {
        ++i;
        if (!read_number(minor))
        {
            return false;
        }
    }
    if (i < text.size() && text[i] == '.')
    {
        ++i;
        if (!read_number(patch))
        {
            return false;
        }
    }

    return true;
}

int compare_version(const std::string &a, const std::string &b)
{
    int a_major = 0, a_minor = 0, a_patch = 0;
    int b_major = 0, b_minor = 0, b_patch = 0;
    if (!parse_version(a, a_major, a_minor, a_patch) || !parse_version(b, b_major, b_minor, b_patch))
    {
        return 0;
    }
    if (a_major != b_major) return (a_major > b_major) ? 1 : -1;
    if (a_minor != b_minor) return (a_minor > b_minor) ? 1 : -1;
    if (a_patch != b_patch) return (a_patch > b_patch) ? 1 : -1;
    return 0;
}

} // namespace

bool ota_update_from_url(const char *https_url, const OtaUpdateOptions &options)
{
    if (https_url == nullptr || https_url[0] == '\0')
    {
        ESP_LOGE(TAG, "OTA URL is empty");
        return false;
    }
    if (std::strncmp(https_url, "https://", 8) != 0)
    {
        ESP_LOGE(TAG, "OTA URL must be https:// (got: %s)", https_url);
        return false;
    }

    if (options.sync_time)
    {
        // TLS needs a roughly-correct system time for certificate validation.
        // Wait up to ~20 seconds for SNTP.
        const bool ok = sync_time_sntp(options.ntp_server, pdMS_TO_TICKS(20000));
        ESP_LOGI(TAG, "SNTP time sync: %s", ok ? "OK" : "FAILED/SKIPPED");
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = https_url;
    http_cfg.timeout_ms = options.timeout_ms;
    http_cfg.keep_alive_enable = true;
    http_cfg.max_redirection_count = options.max_redirects;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach; // requires cert bundle enabled in menuconfig

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;

    ESP_LOGI(TAG, "Starting OTA from: %s", https_url);
    const esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_https_ota failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "OTA successful");
    if (options.restart_on_success)
    {
        ESP_LOGI(TAG, "Restarting...");
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }

    return true;
}

bool ota_check_and_update(const char *version_url,
                          const char *firmware_url,
                          const OtaUpdateOptions &options)
{
    if (options.sync_time)
    {
        const bool ok = sync_time_sntp(options.ntp_server, pdMS_TO_TICKS(20000));
        ESP_LOGI(TAG, "SNTP time sync: %s", ok ? "OK" : "FAILED/SKIPPED");
    }

    std::string remote_ver;
    if (!http_get_text(version_url, options, remote_ver))
    {
        ESP_LOGW(TAG, "Failed to fetch remote version");
        return false;
    }

    const esp_app_desc_t *desc = esp_app_get_description();
    const std::string local_ver = desc ? std::string(desc->version) : std::string();

    if (local_ver.empty())
    {
        ESP_LOGW(TAG, "Local app version is empty; skipping OTA check");
        return false;
    }

    const int cmp = compare_version(remote_ver, local_ver);
    if (cmp <= 0)
    {
        ESP_LOGI(TAG, "No update. Local: %s | Remote: %s", local_ver.c_str(), remote_ver.c_str());
        return false;
    }

    ESP_LOGI(TAG, "Update available. Local: %s | Remote: %s", local_ver.c_str(), remote_ver.c_str());
    return ota_update_from_url(firmware_url, options);
}
