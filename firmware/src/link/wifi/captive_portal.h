#pragma once
#include <stdbool.h>
struct WifiConfig;

// First-run / re-provisioning hotspot. SSID "Clawdmeter", open, 192.168.4.1.
// Any hostname resolves to the portal (DNS wildcard) and the OS captive-portal
// probe URLs redirect to "/", so phones pop the "sign in to network" sheet.
// POST /save writes NVS and restarts the device.
void portal_start(const WifiConfig& current);
void portal_tick(void);
void portal_stop(void);
bool portal_active(void);
