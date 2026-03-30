#pragma once

// Initializes NVS + default event loop, starts WifiManager (AP config portal if needed),
// and spawns a small monitor task that logs connection state.
//
// Returns true if WifiManager initialized successfully.
bool wifi_app_start();

