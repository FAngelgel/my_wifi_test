#include "ota_update.h"

#include <cstring>
#include <string>
#include <ctime>

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

bool system_time_looks_valid()
{
    // If time is already set to a recent epoch, TLS cert validation should work.
    // 1700000000 ~= 2023-11-14.
    std::time_t now = 0;
    std::time(&now);
    return now >= 1700000000;
}

struct HttpCollectCtx
{
    std::string *out = nullptr;
    size_t max_bytes = 0;
    bool overflow = false;
};

esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    auto *ctx = static_cast<HttpCollectCtx *>(evt->user_data);
    if (ctx == nullptr || ctx->out == nullptr)
    {
        return ESP_OK;
    }

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data != nullptr && evt->data_len > 0)
    {
        // When auto-redirect is enabled, esp_http_client may deliver body data for intermediate
        // redirect responses (e.g., 302 HTML). For version.txt we only care about the final 200 body.
        const int status = esp_http_client_get_status_code(evt->client);
        if (status != 200)
        {
            return ESP_OK;
        }

        if (ctx->out->size() + static_cast<size_t>(evt->data_len) > ctx->max_bytes)
        {
            ctx->overflow = true;
            return ESP_FAIL;
        }
        ctx->out->append(static_cast<const char *>(evt->data), static_cast<size_t>(evt->data_len));
    }

    return ESP_OK;
}

bool sync_time_sntp(const char *server, TickType_t wait_ticks)
{
    if (system_time_looks_valid())
    {
        return true;
    }

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

    const int attempts = (options.retries < 1) ? 1 : options.retries;
    for (int attempt = 1; attempt <= attempts; ++attempt)
    {
        out_text.clear();

        esp_http_client_config_t http_cfg = {};
        http_cfg.url = https_url;
        http_cfg.timeout_ms = options.timeout_ms;
        http_cfg.keep_alive_enable = true;
        http_cfg.user_agent = options.user_agent;
        http_cfg.buffer_size = options.rx_buffer_size;
        http_cfg.buffer_size_tx = options.tx_buffer_size;
        http_cfg.disable_auto_redirect = false;
        http_cfg.max_redirection_count = options.max_redirects;
        http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
        HttpCollectCtx collect_ctx{.out = &out_text, .max_bytes = kMaxVersionTextBytes, .overflow = false};
        http_cfg.user_data = &collect_ctx;
        http_cfg.event_handler = &http_event_handler;

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client)
        {
            ESP_LOGE(TAG, "esp_http_client_init failed");
            return false;
        }

        const esp_err_t err = esp_http_client_perform(client);
        const int status = esp_http_client_get_status_code(client);
        const int content_length = esp_http_client_get_content_length(client);
        char final_url[512] = {};
        if (esp_http_client_get_url(client, final_url, sizeof(final_url)) == ESP_OK && final_url[0] != '\0')
        {
            ESP_LOGI(TAG, "Version final URL: %s", final_url);
        }
        ESP_LOGI(TAG, "Version HTTP Status = %d, content_length = %d", status, content_length);

        if (err == ESP_OK && !collect_ctx.overflow && status == 200)
        {
            esp_http_client_cleanup(client);
            break;
        }

        if (collect_ctx.overflow)
        {
            ESP_LOGE(TAG, "Version text too large (> %d bytes)", kMaxVersionTextBytes);
            esp_http_client_cleanup(client);
            return false;
        }

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_http_client_perform failed (attempt %d/%d): %s",
                     attempt, attempts, esp_err_to_name(err));
        }
        else
        {
            if (!out_text.empty())
            {
                ESP_LOGW(TAG, "Unexpected HTTP status for version (attempt %d/%d): %d | body: %.*s",
                         attempt,
                         attempts,
                         status,
                         static_cast<int>(out_text.size()),
                         out_text.c_str());
            }
            else
            {
                ESP_LOGW(TAG, "Unexpected HTTP status for version (attempt %d/%d): %d (empty body)",
                         attempt,
                         attempts,
                         status);
            }

            // Non-200 means the URL/content is wrong; don't keep retrying.
            esp_http_client_cleanup(client);
            return false;
        }

        esp_http_client_cleanup(client);
        if (attempt < attempts && options.retry_delay_ms > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(options.retry_delay_ms));
        }
    }

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
        const TickType_t wait_ticks = pdMS_TO_TICKS(options.time_sync_timeout_ms);
        const bool ok = sync_time_sntp(options.ntp_server, wait_ticks);
        ESP_LOGI(TAG, "SNTP time sync: %s", ok ? "OK" : "FAILED/SKIPPED");
    }

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = https_url;
    http_cfg.timeout_ms = options.timeout_ms;
    http_cfg.keep_alive_enable = true;
    http_cfg.user_agent = options.user_agent;
    http_cfg.buffer_size = options.rx_buffer_size;
    http_cfg.buffer_size_tx = options.tx_buffer_size;
    http_cfg.disable_auto_redirect = false;
    http_cfg.max_redirection_count = options.max_redirects;
    http_cfg.crt_bundle_attach = esp_crt_bundle_attach; // requires cert bundle enabled in menuconfig

    esp_https_ota_config_t ota_cfg = {};
    ota_cfg.http_config = &http_cfg;
    ota_cfg.partial_http_download = options.partial_http_download;
    ota_cfg.max_http_request_size = options.max_http_request_size;
    ota_cfg.bulk_flash_erase = options.bulk_flash_erase;

    const int attempts = (options.retries < 1) ? 1 : options.retries;
    esp_err_t ret = ESP_FAIL;
    for (int attempt = 1; attempt <= attempts; ++attempt)
    {
        ESP_LOGI(TAG, "Starting OTA (attempt %d/%d) from: %s", attempt, attempts, https_url);
        ret = esp_https_ota(&ota_cfg);
        if (ret == ESP_OK)
        {
            break;
        }
        ESP_LOGE(TAG, "esp_https_ota failed (attempt %d/%d): %s", attempt, attempts, esp_err_to_name(ret));
        if (attempt < attempts && options.retry_delay_ms > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(options.retry_delay_ms));
        }
    }
    if (ret != ESP_OK)
    {
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
        const TickType_t wait_ticks = pdMS_TO_TICKS(options.time_sync_timeout_ms);
        const bool ok = sync_time_sntp(options.ntp_server, wait_ticks);
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
