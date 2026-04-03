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
    const char *ntp_server = "pool.ntp.org";
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
