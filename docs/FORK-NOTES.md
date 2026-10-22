# Fork notes

This fork adds UK GMT/BST daylight saving on top of PR #202 (standalone WiFi). See the top of the README for the summary.

## Remotes
- `origin`: this fork (Adman1020/Clawdmeter). Push here.
- `pr-author`: markmcgookin/Clawdmeter, source of `feature/wifi-link` (PR #202).
- `upstream`: HermannBjorgvin/Clawdmeter, the original project.

Branches: `feature/wifi-link` mirrors the PR branch; `local/london-tz` (default) is the PR plus this fork's commits. To pick up PR changes: `git fetch pr-author && git rebase pr-author/feature/wifi-link`, then `git push --force-with-lease origin local/london-tz`.

## Build and flash
- PlatformIO lives in a venv outside the repo (`~/.pio-venv`; needs `python3-venv`).
- `~/.pio-venv/bin/pio run -d firmware -e waveshare_amoled_216_wifi`
- Output: `firmware/.pio/build/waveshare_amoled_216_wifi/firmware.bin` (app only) and `firmware.factory.bin` (bootloader + partitions + app).
- **Updating a working device:** flash `firmware.bin` at offset **0x10000** with "Erase all flash" unticked. This keeps the WiFi, token and timezone stored in NVS.
- **First flash only:** `firmware.factory.bin` at 0x0. It is blank-padded across NVS (0x9000-0xE000), so it **wipes all saved settings** and you must re-provision. Flash from Chrome/Edge with https://espressif.github.io/esptool-js/ (WSL can't see USB without usbipd).
- If the port doesn't appear, hold BOOT while plugging in USB to enter download mode.
- Host tests: `pio test -e sim` needs SDL2. Without it, compile directly: `g++ -std=gnu++17 -Ifirmware/src firmware/test/test_usage_payload/test_main.cpp firmware/src/link/wifi/usage_payload.cpp`.
- Provisioning: join hotspot "Clawdmeter", open 192.168.4.1, enter 2.4 GHz WiFi, a `claude setup-token` token and "UK/Ireland (GMT/BST, auto DST)". Credentials live only in the device's NVS, never in this repo.

## Auto power-off
On battery (no USB), the device does a full PMU shutdown 10 minutes after the screen blanks (screen blanks after 30 min idle). Press PWR to wake it. Tune with `AUTOOFF_AFTER_SLEEP_MS` in `firmware/src/idle_cfg.h` (0 disables). Implemented via `power_hal_power_off()`; a no-op on boards without a PMU.

## Not done
- The six Copilot review findings on PR #202 (see `wifi-handoff.md`).
- Encrypting the stored token behind a PIN.

## Privacy
Commits use the GitHub noreply address. Keep it that way: `git config user.email` is set in this clone.
