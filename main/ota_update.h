#pragma once

#include <cstdint>

struct OtaUpdateOptions
{
    // Try to sync system time using SNTP before starting HTTPS OTA (recommended for TLS).
    bool sync_time = true;

    // Restart after a successful update.
    bool restart_on_success = true;

    // Total OTA network timeout for the HTTP client.
    int timeout_ms = 15000;

    // Max number of HTTP redirects to follow (GitHub release asset URLs redirect).
    int max_redirects = 5;

    // NTP server used when sync_time=true.
    // In some networks (e.g. China), pool.ntp.org may be slow/unreachable.
    const char *ntp_server = "ntp.aliyun.com";

    // How long to wait for SNTP sync (when sync_time=true).
    int time_sync_timeout_ms = 30000;

    // Optional HTTP User-Agent string. Some servers/proxies behave better with a UA set.
    const char *user_agent = "EEG_detect-ota";

    // Increase HTTP client buffers for GitHub redirect headers (Location can be long).
    int rx_buffer_size = 8192;
    int tx_buffer_size = 2048;

    // Break the OTA image download into multiple HTTP requests (Range requests).
    // Helps on flaky networks because each request is shorter.
    bool partial_http_download = true;
    int max_http_request_size = 32 * 1024;

    // Optional: erase the whole OTA partition before writing (can be faster, but may hit watchdog on huge partitions).
    bool bulk_flash_erase = false;

    // Network in some regions can be flaky for GitHub; retry helps a lot.
    int retries = 3;
    int retry_delay_ms = 2000;
};

// Perform OTA update from a HTTPS URL (e.g. GitHub Releases asset URL).
//
// Example URL:
//   https://github.com/<owner>/<repo>/releases/download/v1.0.0/EEG_detect.bin
//
// Returns true if OTA completed successfully. If restart_on_success is true,
// the device will call esp_restart() and this function will not return.
bool ota_update_from_url(const char *https_url, const OtaUpdateOptions &options = {});

// Check a remote version text file and update if newer than current app version.
// version_url should point to a small text file (e.g. "v1.2.3").
// firmware_url should point to the .bin image (e.g. GitHub Releases asset URL).
bool ota_check_and_update(const char *version_url,
                          const char *firmware_url,
                          const OtaUpdateOptions &options = {});
