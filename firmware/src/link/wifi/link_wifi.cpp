// WiFi transport: the device polls the Anthropic API itself. Implements
// link.h over wifi_poller / provisioning / captive_portal. All user-facing
// text is formatted here, on the loop task, into this file's static buffers.
#include "link/link.h"
#include "link/wifi/wifi_poller.h"
#include "link/wifi/provisioning.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

static WifiConfig s_cfg;
static char s_buf[320];
static char s_ssid[40];
static char s_detail[48];

void link_init(void) {
    prov_load(&s_cfg);
    poller_start(s_cfg);
}

void link_tick(void) {}

static const char* detail_text(poller_err_t e, int code) {
    switch (e) {
        case PERR_NONE:         return "";
        case PERR_NO_NETWORK:   return "No network";
        case PERR_WAIT_TIME:    return "Waiting for time";
        case PERR_NO_TOKEN:     return "No token - hold PWR to set up";
        case PERR_BAD_TOKEN:    return "Bad token (401)";
        case PERR_RATE_LIMITED: return "Rate limited (429)";
        case PERR_TLS:          return "TLS error";
        case PERR_NO_HEADERS:   return "No usage headers";
        case PERR_TASK:         return "Task start failed";
        case PERR_API:
            snprintf(s_detail, sizeof s_detail, "API error %d", code);
            return s_detail;
    }
    return "";
}

LinkStatus link_status(void) {
    LinkStatus s;
    s.gen = poller_gen();
    poller_err_t err = poller_error();
    s.detail = detail_text(err, poller_error_code());
    snprintf(s_ssid, sizeof s_ssid, "%s", s_cfg.ssid);   // SSID on its own line; titles stay short (I6)

    switch (poller_phase()) {
        case POLLER_PORTAL:
            s.state = LINK_WAITING;
            s.line1 = "To set up WiFi"; s.line2 = "join hotspot Clawdmeter"; s.line3 = "then open 192.168.4.1";
            s.detail = "";
            break;
        case POLLER_CONNECTING:
        case POLLER_WAIT_TIME:
            s.state = poller_had_success() ? LINK_DISCONNECTED : LINK_WAITING;
            s.line1 = "Connecting"; s.line2 = s_ssid; s.line3 = "";
            break;
        case POLLER_POLLING:
            if (poller_last_poll_ok() || err == PERR_RATE_LIMITED) {
                // Fresh payload (429 still carries the headers) → live usage view.
                s.state = LINK_CONNECTED;
                s.line1 = ""; s.line2 = ""; s.line3 = "";
            } else if (!poller_had_success() && err == PERR_NONE) {
                s.state = LINK_WAITING;
                s.line1 = "Connected"; s.line2 = s_ssid; s.line3 = "fetching usage";
            } else if (err == PERR_NO_NETWORK && poller_had_success()) {
                s.state = LINK_DISCONNECTED;
                s.line1 = "Connected"; s.line2 = s_ssid; s.line3 = "";
            } else {
                // Token revoked, API error, missing headers, no token: say so
                // instead of silently showing stale numbers (spec error table).
                s.state = LINK_WAITING;
                s.line1 = "Connected"; s.line2 = s_ssid; s.line3 = "";
            }
            break;
        default:
            s.state = LINK_INIT; s.line1 = "Starting"; s.line2 = ""; s.line3 = ""; s.detail = "";
    }
    return s;
}

bool        link_has_data(void)        { return poller_has_payload(); }
const char* link_get_data(void)        { poller_take_payload(s_buf, sizeof s_buf); return s_buf; }
void        link_ack(void)             {}
void        link_nack(void)            { Serial.println("wifi: payload rejected by parser"); }
void        link_request_refresh(void) {}
void        link_on_pair_gesture(void) { Serial.println("wifi: pair gesture - toggling hotspot"); poller_request_portal(); }
bool link_serial_cmd(const char* line) {
    bool handled = prov_handle_serial(line, &s_cfg);
    if (handled && strcmp(line, "status") == 0) {
        static const char* PH[] = { "idle", "connecting", "waiting-for-time", "polling", "portal" };
        LinkStatus st = link_status();
        Serial.printf("link: phase=%s state=%d ok_once=%d detail=\"%s\"\n",
                      PH[poller_phase()], (int)st.state, poller_had_success() ? 1 : 0, st.detail);
    }
    return handled;
}
