#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct WifiConfig;

enum poller_phase_t { POLLER_IDLE, POLLER_CONNECTING, POLLER_WAIT_TIME, POLLER_POLLING, POLLER_PORTAL };

// Why the last step failed. Formatting into user text happens in link_wifi.cpp
// on the loop task, so the poller never hands out a buffer it may rewrite.
enum poller_err_t {
    PERR_NONE,
    PERR_NO_NETWORK,     // STA not connected / request never reached the server
    PERR_WAIT_TIME,      // SNTP not synced yet
    PERR_NO_TOKEN,       // provisioned WiFi but no token
    PERR_BAD_TOKEN,      // HTTP 401
    PERR_RATE_LIMITED,   // HTTP 429 (payload still published; headers are present)
    PERR_API,            // other HTTP status, see poller_error_code()
    PERR_TLS,            // handshake / begin() failure
    PERR_NO_HEADERS,     // 200 without any utilization header
    PERR_TASK,           // poller task could not be created
};

void poller_start(const WifiConfig& cfg);   // spawns the FreeRTOS task
void poller_request_portal(void);            // PWR gesture: open the hotspot (again → close it)

uint32_t       poller_gen(void);             // bumps on every phase / error change
poller_phase_t poller_phase(void);
poller_err_t   poller_error(void);
int            poller_error_code(void);      // HTTP status for PERR_API
bool           poller_had_success(void);     // >= 1 published payload since boot
bool           poller_last_poll_ok(void);    // most recent poll published a payload

// Payload hand-off (thread-safe). take_payload copies into dst and clears the flag.
bool  poller_has_payload(void);
bool  poller_take_payload(char* dst, size_t cap);
