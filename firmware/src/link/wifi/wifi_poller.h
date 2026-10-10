#pragma once
#include <stdbool.h>
#include <stddef.h>
struct WifiConfig;

enum poller_phase_t { POLLER_IDLE, POLLER_CONNECTING, POLLER_WAIT_TIME, POLLER_POLLING, POLLER_PORTAL };

void poller_start(const WifiConfig& cfg);   // spawns the FreeRTOS task
void poller_request_portal(void);            // from pair gesture; task opens the hotspot
poller_phase_t poller_phase(void);
bool  poller_had_success(void);              // >= 1 HTTP 200 since boot
const char* poller_error(void);              // "" or static message, e.g. "Bad token (401)"
int   poller_connect_failures(void);

// Payload hand-off (thread-safe). take_payload copies into dst and clears the flag.
bool  poller_has_payload(void);
bool  poller_take_payload(char* dst, size_t cap);
