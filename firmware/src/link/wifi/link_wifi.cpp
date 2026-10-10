// WiFi transport: the device polls the Anthropic API itself. Implements
// link.h over wifi_poller / provisioning / captive_portal.
#include "link/link.h"
#include "link/wifi/wifi_poller.h"
#include "link/wifi/provisioning.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

static WifiConfig s_cfg;
static char s_buf[320];
static char s_line1[48];
static char s_line2[48];

void link_init(void) {
    prov_load(&s_cfg);
    poller_start(s_cfg);
}

void link_tick(void) {}

LinkStatus link_status(void) {
    LinkStatus s;
    s.detail = poller_error();
    switch (poller_phase()) {
        case POLLER_PORTAL:
            s.state = LINK_WAITING;
            s.line1 = "To set up WiFi"; s.line2 = "join hotspot Clawdmeter"; s.line3 = "then open 192.168.4.1";
            s.detail = "";
            break;
        case POLLER_CONNECTING:
        case POLLER_WAIT_TIME:
            s.state = poller_had_success() ? LINK_DISCONNECTED : LINK_WAITING;
            snprintf(s_line2, sizeof s_line2, "%s", s_cfg.ssid);
            s.line1 = "Connecting"; s.line2 = s_line2; s.line3 = "";
            break;
        case POLLER_POLLING:
            if (poller_had_success()) {
                s.state = LINK_CONNECTED;
                s.line1 = ""; s.line2 = ""; s.line3 = "";
            } else {
                s.state = LINK_WAITING;
                snprintf(s_line1, sizeof s_line1, "Connected to %s", s_cfg.ssid);
                s.line1 = s_line1; s.line2 = "fetching usage"; s.line3 = "";
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
void        link_on_pair_gesture(void) { Serial.println("wifi: pair gesture - opening hotspot"); poller_request_portal(); }
bool        link_serial_cmd(const char* line) { return prov_handle_serial(line, &s_cfg); }
