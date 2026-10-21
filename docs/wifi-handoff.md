# Clawdmeter standalone WiFi: handoff for Claude Code

Written 2026-10-13. Everything here comes from reading the GitHub pages linked below. Nothing has been built, flashed or tested by me.

## Goal

Run a Claude Code usage dashboard that is **entirely self-contained on the device**: it joins WiFi and polls the Anthropic API itself, with no laptop, daemon or Bluetooth link required. It should be as simple as possible and "just work".

## Hardware I own

- Waveshare ESP32-S3-Touch-AMOLED-2.16 (**use this one**)
- Waveshare ESP32-C6-Touch-AMOLED-2.16 (not covered by the PR below)

Location is the UK, so the timezone is Europe/London (GMT/BST).

## Background

- Upstream project: https://github.com/HermannBjorgvin/Clawdmeter
  - Boards supported out of the box include both of mine.
  - The device is Bluetooth-only. A host daemon polls `api.anthropic.com/v1/messages` (one Haiku token) and reads the `anthropic-ratelimit-unified-*` response headers. It then writes a small JSON payload to the board over a BLE GATT characteristic.
  - The firmware is a thin hardware-abstraction layer with per-board folders under `firmware/src/boards/`, built with PlatformIO. Porting notes are in `docs/porting/`.
- Other projects I looked at and why they weren't chosen:
  - VibePulse (niclasvestlund-YT/vibepulse): same S3 board over WiFi, but needs a macOS-only Python service.
  - Claude Usage Stick (oauramos/claude-usage-stick): standalone, but supports LilyGo and small ESP32 boards, not Waveshare AMOLED.
  - ClaudeGauge (dorofino/ClaudeGauge): needs a Vercel proxy, and is for LilyGo and Waveshare 1.47 boards.

## The solution: PR #202

https://github.com/HermannBjorgvin/Clawdmeter/pull/202
"feat: standalone WiFi mode for AMOLED-2.16 (polls Anthropic API directly, no daemon)"

- Author: markmcgookin. Branch: `markmcgookin/Clawdmeter` at `feature/wifi-link`.
- Status when I looked (2026-10-13): **open, not merged**, opened 2026-10-12, 11 commits, Copilot review done.
- New PlatformIO env: `waveshare_amoled_216_wifi`. The existing BLE envs are meant to be unaffected.
- How it works:
  - A new transport interface, `firmware/src/link/link.h`, with `link_ble.cpp` (the existing BLE path) and `link/wifi/` (the new WiFi path).
  - A FreeRTOS poller does the 1-token Haiku `POST /v1/messages` and builds the same JSON payload the daemon sends, so the UI code is shared.
  - TLS uses a pinned GTS Root R4. Buffers and LVGL's memory pool live in PSRAM. The poller stack is in internal SRAM, because a PSRAM stack stalled WiFi init on hardware.
  - BLE HID (the side buttons sending Space and Shift+Tab) is kept. The BLE data service is compiled out in the WiFi build.
- Provisioning:
  - Captive portal: hotspot "Clawdmeter" at 192.168.4.1. Serial commands also work.
  - Credentials are stored in NVS.
  - The PWR button toggles the hotspot.
  - The hotspot auto-closes after 5 minutes unattended once credentials have connected.
- Auth: a long-lived token from `claude setup-token`. The OAuth refresh token is not used on the device. `/api/oauth/usage` returns 403 for this token, so only the Messages response headers are used.

### What the author says is tested

- Host tests for the payload builder (`firmware/test/test_usage_payload`).
- All existing envs and the new one build.
- On a real 2.16 board: provisioning from an iPhone, STA connect, SNTP, `poll ok (200)`, usage tracking real usage for an hour, and surviving reboot and battery power.

### What is NOT tested, per the PR

- The bad-token (401) hint path.
- HID coexistence with WiFi.
- A Bluetooth board after the refactor.

### Copilot review: 6 open findings

Medium:
1. Clear the stored password when switching to an open network (`captive_portal.cpp`).
2. Preserve cached usage until freshness expires. Currently some failures flip to "Connected" with no numbers (`link_wifi.cpp`).
3. Preserve task-creation failure details (`link_wifi.cpp`).
4. Make the reconnect delay observe `s_portal_req` (`wifi_poller.cpp`).

Low:
5. Reconcile the end-to-end poll credential verification records (`CLAUDE.md` vs the commit notes).
6. Document the internal SRAM requirement for the poller stack. The spec says the stack is in PSRAM, which contradicts the code.

## Known caveats for me

- **No DST.** The timezone is a fixed offset, so reset times will be an hour off for part of the year. UK clocks go back on 25 Oct 2026.
- The token is typed over an open hotspot and stored in plain NVS. The README says so. Claude Usage Stick encrypts it behind a PIN, which is a pattern worth copying if wanted.
- The model id is hard-coded.
- 2.4 GHz WiFi only. The ESP32-S3 can't see 5 GHz networks.
- Flash use was 86.3% on this env at the time of the commit. Adding features may hit the limit.

## Suggested tasks for Claude Code

1. Clone the PR branch:
   ```
   git clone -b feature/wifi-link https://github.com/markmcgookin/Clawdmeter.git
   ```
   Read the "Standalone WiFi mode" section of the README and `CLAUDE.md` (gotchas 13-15).
2. Install PlatformIO and build the `waveshare_amoled_216_wifi` env. Build the existing envs too, to check nothing regressed.
3. Run the host tests in `firmware/test/test_usage_payload`.
4. Flash the S3 board, provision it through the hotspot or serial, and confirm polling works.
5. Fix the timezone and DST handling for Europe/London. Check how the firmware currently stores the offset and the reset-time maths, and use a POSIX TZ string such as `GMT0BST,M3.5.0/1,M10.5.0` with `setenv("TZ", ...)` and `tzset()` if it fits.
6. Optionally address the Copilot findings above, especially findings 1 and 2.
7. Optionally encrypt the stored token behind a PIN, as in Claude Usage Stick.
8. Keep local changes on a branch, so the upstream PR can still be pulled in.

## Sources

- https://github.com/HermannBjorgvin/Clawdmeter
- https://github.com/HermannBjorgvin/Clawdmeter/pull/202
- https://github.com/niclasvestlund-YT/vibepulse/wiki
- https://github.com/oauramos/claude-usage-stick
- https://www.hackster.io/dorofino/claudegauge-real-time-ai-usage-monitor-on-esp32-s3-with-a-a82d4b
