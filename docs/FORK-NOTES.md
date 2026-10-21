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
- Output: `firmware/.pio/build/waveshare_amoled_216_wifi/firmware.factory.bin`. Flash at offset 0x0, e.g. from Chrome/Edge with https://espressif.github.io/esptool-js/ (works; WSL can't see USB without usbipd).
- Host tests: `pio test -e sim` needs SDL2. Without it, compile directly: `g++ -std=gnu++17 -Ifirmware/src firmware/test/test_usage_payload/test_main.cpp firmware/src/link/wifi/usage_payload.cpp`.
- Provisioning: join hotspot "Clawdmeter", open 192.168.4.1, enter 2.4 GHz WiFi, a `claude setup-token` token and "UK/Ireland (GMT/BST, auto DST)". Credentials live only in the device's NVS, never in this repo.

## Not done
- The six Copilot review findings on PR #202 (see `wifi-handoff.md`).
- Encrypting the stored token behind a PIN.
- A multi-provider pager was considered and dropped (no good official data sources besides GitHub Copilot's personal billing API).

## Privacy
Commits use the GitHub noreply address. Keep it that way: `git config user.email` is set in this clone.
