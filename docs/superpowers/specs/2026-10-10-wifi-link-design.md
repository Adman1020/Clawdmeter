# WiFi link: standalone usage polling over HTTPS

**Date:** 2026-10-10
**Status:** approved design
**Target:** `waveshare_amoled_216` first; any PSRAM S3 board can opt in later.

## Goal

Let a Clawdmeter show Claude Code usage with no laptop, daemon, or BLE data
connection. The device joins the user's WiFi, calls the Anthropic API itself,
and renders the same screens it does today. Everything that exists today
(seven boards, the simulator, the BLE daemon path) keeps working unchanged.

## Non-goals

- OAuth login or token refresh on the device. The upstream maintainer has
  asked that the refresh token never be used by the device (issue #110): it is
  rate-limited and can log out Claude Code / Claude Desktop.
- The zero-token `/api/oauth/usage` endpoint. The long-lived
  `claude setup-token` has inference scope only and gets HTTP 403 there.
- C6 boards. TLS plus NimBLE does not fit without PSRAM.
- A new on-device WiFi screen. Status lives in the existing hint/status line.
- Changing the daemon or the BLE data protocol.

## Prior art

- Issue #110 (standalone WiFi request; maintainer open to merging).
- `alasdaircs/clawdmeter-wifi`: hard fork, tested on the AMOLED-2.16. We
  borrow its proven mechanics (pinned GTS Root R4 roots, mbedTLS allocations
  routed to PSRAM, PSRAM task stack, 60 s poll with backoff, captive portal)
  but re-implement them behind a transport interface so upstream stays
  mergeable.
- PR #40: WiFi to a host-side HTTP bridge. Not what we want (still needs a
  laptop).

## Architecture

```
                 ┌──────────── shared code (unchanged logic) ────────────┐
main.cpp ──▶ link_* API ──▶ parse_json ──▶ usage_rate / chime / ui_update
                 │
      ┌──────────┴──────────┐
 link_ble.cpp           link_wifi.cpp
 (wraps ble.cpp         (wifi_poller + provisioning + captive_portal)
  data service)
```

### 1. Transport interface (`firmware/src/link/link.h`)

```c
enum link_state_t { LINK_INIT, LINK_WAITING, LINK_CONNECTED, LINK_DISCONNECTED };
//  WAITING   = advertising (BLE) / unprovisioned portal open (WiFi)
//  CONNECTED = central connected (BLE) / STA connected + at least one 200 (WiFi)

struct LinkStatus {
    link_state_t state;
    const char*  line1;   // hint title, e.g. "To pair" / "To set up WiFi"
    const char*  line2;   // e.g. "hold the power button" / "join hotspot Clawdmeter"
    const char*  line3;   // e.g. "for 3 seconds, then release" / "then open 192.168.4.1"
    const char*  detail;  // optional: SSID + last error, or ""
};

void        link_init(void);
void        link_tick(void);
LinkStatus  link_status(void);
bool        link_has_data(void);
const char* link_get_data(void);   // JSON payload, same schema the daemon sends
void        link_ack(void);
void        link_nack(void);
void        link_request_refresh(void);
void        link_on_pair_gesture(void);   // BLE: clear bonds. WiFi: reopen portal.
```

`main.cpp` swaps every `ble_has_data / ble_get_data / ble_send_ack /
ble_send_nack / ble_request_refresh / ble_clear_bonds` call for the `link_*`
equivalent. It keeps calling `ble_init/ble_tick/ble_keyboard_* /
ble_set_battery_level` directly because HID is not a transport concern.

`ui.h` stops including `ble.h`. `ui_update_ble_status(ble_state_t, name, mac)`
becomes `ui_update_link_status(const LinkStatus&)`. The pairing-hint labels and
the "Listening / No data" status text read from the struct instead of
hard-coded strings. The pair/idle/usage view-state logic is unchanged:
`state == LINK_CONNECTED` replaces `s_ble_connected`.

### 2. BLE implementation (`link/link_ble.cpp`)

Pure adapter over the existing `ble.cpp` functions. Status strings are today's
"To pair / hold the power button / for 3 seconds, then release". Behaviour is
byte-for-byte what main does now. Included by every existing hardware env.

`boards/sim/ble_sim.cpp` keeps stubbing `ble.h`; the sim env includes
`link_ble.cpp` so scenario playback is untouched.

### 3. WiFi implementation (`link/link_wifi.cpp` + helpers)

Files, all under `firmware/src/link/wifi/`:

- `provisioning.{h,cpp}`: NVS (`Preferences` namespace `wifi`) with keys
  `ssid`, `pass`, `token`, `tz` (minutes east of UTC, int), `tf` (12/24, 0 =
  unset). Serial commands handled from main's `check_serial_cmd`:
  `ssid <s>`, `pass <s>`, `token <s>`, `tz <mins>`, `status`, `wifi-reset`.
  `status` prints SSID, token length and last 4 chars, tz, link state. Never
  prints the full token.
- `captive_portal.{h,cpp}`: `WiFi.softAP("Clawdmeter")`, `DNSServer` wildcard
  to 192.168.4.1, `WebServer` with `GET /` (form), `POST /save` (writes NVS,
  responds "Saved, rebooting", `ESP.restart()` after 1 s), and the usual
  OS probe paths (`/generate_204`, `/hotspot-detect.html`,
  `/connecttest.txt`, `/ncsi.txt`) redirecting to `/`. Form fields: SSID,
  password, token, timezone offset (select of common UTC offsets, default
  from a `+0:00` entry), hour format (auto/12/24). Served HTML is a single
  PROGMEM string, no external assets.
- `usage_payload.{h,cpp}`: **pure, host-testable.** Input: the eight
  `anthropic-ratelimit-unified-*` header values as strings (empty when
  absent), `now` epoch, tz minutes, hour format. Output: the JSON payload in
  the daemon's exact schema (`s, sr, w, wr, st, acct, tp, pd, rd, t, tf, c,
  ok`). Enterprise period math (`tp`, `pd`, `rd`) is ported from the daemon's
  Python using `gmtime_r`/`timegm`-style arithmetic, no libc locale. `c`
  (chime) is always 1; the firmware's existing reset detector still gates it.
- `wifi_poller.{h,cpp}`: FreeRTOS task on core 0, 12 KB stack allocated in
  PSRAM. State machine: `CONNECTING → SNTP → POLLING`, with `PORTAL` entered
  when NVS has no SSID or after 3 consecutive connect failures, or on
  `link_on_pair_gesture()`. Poll = `POST https://api.anthropic.com/v1/messages`
  with headers `Authorization: Bearer <token>`, `anthropic-version:
  2023-06-01`, `anthropic-beta: oauth-2025-04-20`, `Content-Type:
  application/json`, `User-Agent: Clawdmeter/<ver>`; body is the daemon's
  1-token Haiku message. Response body is discarded; headers are collected via
  `HTTPClient::collectHeaders`. On 200 → build payload, publish to a
  mutex-guarded buffer that `link_has_data()` exposes, interval 60 s. On
  failure → interval doubles to a 300 s cap, `detail` set to one of
  "Bad token (401)", "Rate limited (429)", "API error <code>", "No network",
  "TLS error". 401 does not open the portal (the user re-enters via gesture).
  TLS: `WiFiClientSecure::setCACert` with GTS Root R4 K1 + K2 concatenated;
  `mbedtls_platform_set_calloc_free` routes allocations ≥ 512 B to PSRAM.
  SNTP via `configTime(0, 0, "pool.ntp.org")`; polling waits for a sane epoch
  (> 2024-01-01) because reset countdowns and Enterprise math need it. Clock
  epoch `t` sent to the UI is `now + tz*60`, matching the daemon's convention.
- `link_wifi.cpp`: glue implementing `link.h` over the above. Status strings:
  - PORTAL: "To set up WiFi" / "join hotspot Clawdmeter" / "then open 192.168.4.1"
  - CONNECTING: "Connecting" / "<ssid>" / ""
  - POLLING with error: "Connected to <ssid>" / "<detail>" / ""
  - CONNECTED (≥1 success): state `LINK_CONNECTED`; view-state shows usage.

### 4. `ble.cpp` in the WiFi env

Guarded by `#ifdef LINK_WIFI`: the custom data GATT service (RX/TX/REQ) is
not created, the scan response does not advertise its UUID, and
`ble_has_data()` returns false. HID keyboard, bonding, owner pruning and the
battery level service are unchanged. `NimBLE MAX_CONNECTIONS` stays 2 (harmless).

### 5. Build

New env in `platformio.ini`:

```ini
[env:waveshare_amoled_216_wifi]
extends = env:waveshare_amoled_216
build_src_filter =
    ${env:waveshare_amoled_216.build_src_filter}
    -<link/link_ble.cpp>
    +<link/wifi/>
build_flags =
    ${env:waveshare_amoled_216.build_flags}
    -DLINK_WIFI
```

Existing envs add `-<link/wifi/>` to their filters (or the shared default
filter excludes `link/wifi/` and the WiFi env re-adds it). No new `lib_deps`:
`WiFi`, `WiFiClientSecure`, `HTTPClient`, `WebServer`, `DNSServer` ship with
the Arduino core.

### 6. Hold-to-pair gesture

Unchanged timing. `pair_tick()` calls `link_on_pair_gesture()` instead of
`ble_clear_bonds()`. In the WiFi env that reopens the captive portal; BLE
bonds are cleared there with a new `ble-reset` serial command so HID
re-pairing is still possible.

## Error handling summary

| Condition | Device behaviour |
|---|---|
| No SSID in NVS | Portal, hint shows hotspot instructions |
| Wrong WiFi password ×3 | Portal (same hint) |
| Router down after connect | Reconnect loop, hint "No network", stale data → idle screen after 90 s |
| 401 | Hint "Bad token (401)", backoff, portal only via gesture |
| 429 / 5xx | Hint with code, backoff to 5 min |
| SNTP never syncs | Stays in "Connecting" with detail "Waiting for time" |
| Heap pressure | Task stack and TLS buffers in PSRAM; log free internal heap each poll at DEBUG |

## Testing

- **Host unit tests** (`firmware/test/test_usage_payload/`, g++ like the
  existing `test_splash_geometry`): Pro payload, Enterprise payload including
  month-boundary period math, missing headers → `ok:false`, clock fields on/off,
  reset-in-the-past clamps to 0.
- **Build matrix**: `waveshare_amoled_216`, `waveshare_amoled_216_wifi`, and
  `sim` must all compile. One other board (`waveshare_amoled_18`) compiled to
  prove the filter change is safe.
- **Hardware** on the user's 2.16: provision via phone portal; usage renders
  within ~70 s; HID Space / Shift+Tab still work after BLE pairing; gesture
  reopens portal; wrong token shows the 401 hint; 30 min soak with free-heap
  log stable.

## Documentation

- `README.md`: a "Standalone WiFi mode" section (flash the `_wifi` env, run
  `claude setup-token`, provision via hotspot).
- `CLAUDE.md`: link abstraction, the WiFi env, PSRAM-only constraint, K2 root
  expiry 2028-01-28.

## Trade-offs accepted

- One Haiku output token per poll (~1440/day) — the price of a scope-limited
  long-lived token.
- Pinned GTS Root R4 K2 expires 2028-01-28; documented reflash.
- Token is stored in plain NVS (no flash encryption), same posture as the
  reference fork. Documented.
