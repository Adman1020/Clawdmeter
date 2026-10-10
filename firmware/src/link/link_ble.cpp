// BLE transport adapter: the host daemon pushes JSON over the custom GATT
// data service in ble.cpp. This file only translates names; behaviour is
// exactly what main.cpp did before the link abstraction existed.
#include "link/link.h"
#include "ble.h"

void link_init(void) { /* ble_init() is called by main — HID is not a transport concern */ }
void link_tick(void) { /* ble_tick() likewise stays in main */ }

LinkStatus link_status(void) {
    LinkStatus s;
    switch (ble_get_state()) {
        case BLE_STATE_CONNECTED:    s.state = LINK_CONNECTED;    break;
        case BLE_STATE_DISCONNECTED: s.state = LINK_DISCONNECTED; break;
        case BLE_STATE_ADVERTISING:  s.state = LINK_WAITING;      break;
        default:                     s.state = LINK_INIT;         break;
    }
    s.line1  = "To pair";
    s.line2  = "hold the power button";
    s.line3  = "for 3 seconds, then release";
    s.detail = "";
    s.gen    = (uint32_t)s.state;   // strings are constant; only the state can change
    return s;
}

bool        link_has_data(void)        { return ble_has_data(); }
const char* link_get_data(void)        { return ble_get_data(); }
void        link_ack(void)             { ble_send_ack(); }
void        link_nack(void)            { ble_send_nack(); }
void        link_request_refresh(void) { ble_request_refresh(); }
void        link_on_pair_gesture(void) { ble_clear_bonds(); }
bool        link_serial_cmd(const char* line) { (void)line; return false; }
