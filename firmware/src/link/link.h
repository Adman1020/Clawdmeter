#pragma once
#include <stdbool.h>

// Transport abstraction between main/ui and the usage-data source.
// Two implementations exist, selected per PlatformIO env via build_src_filter:
//   link/link_ble.cpp   — host daemon writes JSON over the custom BLE GATT service
//   link/wifi/*         — the device polls the Anthropic API itself over WiFi/HTTPS
// Shared code must only ever include this header, never a transport's own.

enum link_state_t {
    LINK_INIT,          // not yet started
    LINK_WAITING,       // BLE: advertising. WiFi: portal open / connecting / no success yet
    LINK_CONNECTED,     // BLE: central connected. WiFi: STA up and >=1 successful poll
    LINK_DISCONNECTED,  // BLE: central dropped. WiFi: STA lost after having been connected
};

// Snapshot of what the UI should say about the transport. Pointers are to
// static storage owned by the transport and remain valid until the next
// link_status() call.
struct LinkStatus {
    link_state_t state;
    const char*  line1;   // hint title shown when not connected, e.g. "To pair"
    const char*  line2;   // hint body line 1
    const char*  line3;   // hint body line 2
    const char*  detail;  // short error / context, "" when none
};

void        link_init(void);
void        link_tick(void);
LinkStatus  link_status(void);

// JSON usage payload in the daemon's schema. link_get_data() clears the
// pending flag; the returned pointer is valid until the next link_tick().
bool        link_has_data(void);
const char* link_get_data(void);
void        link_ack(void);
void        link_nack(void);
void        link_request_refresh(void);

// Hold-to-pair gesture completed (PWR held ~3 s then released).
// BLE: clear bonds and re-advertise. WiFi: reopen the captive portal.
void        link_on_pair_gesture(void);

// Give the transport first refusal on a serial command line. Returns true if
// it consumed the line. BLE handles nothing; WiFi handles ssid/pass/token/...
bool        link_serial_cmd(const char* line);
