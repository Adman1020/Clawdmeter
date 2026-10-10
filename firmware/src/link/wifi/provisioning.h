#pragma once
#include <stdbool.h>

// On-device credentials for the WiFi link. Stored in NVS via Preferences
// (namespace "wifi"). The token is the long-lived `claude setup-token`.
struct WifiConfig {
    char ssid[33];
    char pass[65];
    char token[256];
    int  tz_minutes;   // local offset east of UTC; default 0
    int  hour_fmt;     // 0 = clock off, 12 or 24
};

void prov_load(WifiConfig* out);
void prov_save(const WifiConfig& c);
void prov_clear(void);
bool prov_has_wifi(const WifiConfig& c);
bool prov_has_token(const WifiConfig& c);

// Serial provisioning: "ssid <s>", "pass <s>", "token <s>", "tz <mins>",
// "hourfmt <0|12|24>", "status", "wifi-reset". Mutates and saves *cfg.
// Returns true when the line was recognised.
bool prov_handle_serial(const char* line, WifiConfig* cfg);
