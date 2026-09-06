# esp32-board-app-template

A starting point for an **ESP32 board app**: one codebase, many boards, controlled from
a phone over **BLE** and updated over **WiFi**. It gives you the boring-but-fiddly parts
so a new app is mostly `app.cpp`. Three shipping apps are built on it —
[bboink](https://github.com/whitewhidow/bboink) (WiFi handshake hunter),
[bb-portal](https://github.com/whitewhidow/bb-portal) (Wi-Fi terms / access-code portal) and
[hid-ble-poc](https://github.com/whitewhidow/hid-ble-poc) (USB/BLE HID tool) — read any of
them as a worked example.

## What you get

- **Multi-board HAL** (`board.h`) — display pins/panel, button, name per board, selected
  by a build flag. Boards included: T-Embed CC1101, T-Dongle S3, Cardputer ADV (StampS3),
  T-Display C5, Waveshare C5-LCD, and a **generic headless S3**.
- **Display** (`display.cpp`) — LovyanGFX with a **null path** for headless boards
  (a missing panel would otherwise hang init). Splash + centered text + status bar.
- **BLE control service** (`ble_control.cpp`) — the phone portal writes `__CMD__`s, the
  board notifies replies. Built-in: version, WiFi provisioning, self-update, status,
  config (served in MTU-safe chunks so it doesn't silently drop on the C5). Your commands
  via `appHandleCommand()`.
- **WiFi OTA** (`netota.cpp`) — reboot-to-fetch self-update into an A/B slot (works even
  on headless / single-button boards).
- **Config** (`config.cpp`) — NVS settings; declare fields once, the portal auto-renders
  the form. Includes the **boot-splash on/off** toggle, **Export all / Import** of the whole
  config (+ WiFi) as a JSON file, and named **config profiles** saved in the browser.
- **Portal** (`portal/index.html`) — Web-Bluetooth control page: a status header with a
  version pill (checked against the latest GitHub release) plus live board badges
  (battery %, BLE RSSI, uptime, free heap, free flash), and **Editor / Config / WiFi / Update**
  tabs (opens on Editor). A `clock.h` helper takes a phone-set wall clock (`__TIME__`) so apps
  can real-timestamp their data without a radio.
- **Web flasher** (`flasher/`) — ESP Web Tools browser USB flash of the **merged** image.
- **CI** (`.github/workflows/release.yml`) — on a tag, builds every board, publishes
  app-only + merged bins, and auto-updates the flasher.
- **[GOTCHAS.md](GOTCHAS.md)** — the expensive lessons (MAC/GATT cache, notify race,
  StampS3 board setting, buttonless flashing, app-only vs merged bins, …).

## Boards

| env | board | chip | display | notes |
|-----|-------|------|---------|-------|
| `s3-headless`  | Generic ESP32-S3 (8MB) | S3 | none | BLE-only; the simplest start |
| `tembed-cc1101` | LilyGo T-Embed CC1101 | S3 | ST7789 320×170 | |
| `tdongle-s3`    | LilyGo T-Dongle S3    | S3 | ST7735S 80×160 | |
| `cardputer`    | M5Cardputer ADV        | S3 | ST7789 135×240 | `board = m5stack-stamps3` (required) |
| `tdisplay-c5`  | LilyGo T-Display C5   | C5 | ST7789 320×170 | |
| `waveshare-c5` | Waveshare C5-LCD-1.47 | C5 | ST7789 320×172 | |

## Use it

1. **Use this template** on GitHub → your repo. Then rename the template's repo/owner
   strings to yours — list every file that mentions them with:
   `grep -rl 'esp32-board-app-template\|whitewhidow' src portal flasher index.html .github`
   (they live in `version.h`, `release.yml`, the portal, the flasher, and the root
   `index.html`). Set Pages to serve from **`/` (root)**.
2. **Name your app**: set `APP_NAME` in `version.h` (the BLE advertised name + on-screen
   title). Each web page carries its own `const APP_NAME` at the top of its script
   (`portal/`, `flasher/`, root `index.html`) that drives its title/heading — keep those in
   sync with `version.h`.
3. Build/flash: `pio run -e s3-headless -t upload` (add `-e <board>` for others).
4. Write your app in **`app.cpp`** (`appSetup`/`appLoop` + `appHandleCommand`) and add
   settings in **`config.cpp`** (`CFG_FIELDS`). The stock `app.cpp` is a small **demo** you
   can copy from: it edits a LittleFS-backed text file over BLE (Editor tab), showing
   persistent storage + moving a payload bigger than one BLE packet (chunked, base64, acked).
5. Release: `git tag v0.1.0 && git push --tags` — CI publishes bins + the flasher.

## Add a board

The HAL is build-flag driven, so a new board is a few edits — no core changes:

1. **`platformio.ini`** — add an `[env:yourboard]` (chip, flash size, `board`, and a
   `-D APP_BOARD_YOURBOARD` build flag; C5 targets also need the `0x2000` bootloader offset
   + `upload_flags = --no-stub`).
2. **`src/board.h`** — add an `#elif defined(APP_BOARD_YOURBOARD)` block: display pins/panel,
   button pin, battery ADC (if any), LED pin/count, and `APP_BOARD_NAME`.
3. **`src/display.cpp`** — if the panel differs, add its size/offset tier (or let it fall to
   the null path on a headless board).
4. **partitions** — reuse a matching `partitions-*.csv` (16 MB / 8 MB A/B, or the 4 MB
   single-app table) so the OTA slots line up.
5. Add the env to `release.yml`'s board list so CI builds and flashes it.

See [GOTCHAS.md](GOTCHAS.md) for the per-board traps (StampS3 `board=`, C5 bootloader offset,
Waveshare LED R/G swap, single-app 4 MB, …).

## Getting help

Questions or a bug? **Open a GitHub issue** —
<https://github.com/whitewhidow/esp32-board-app-template/issues>. The three apps above
([bboink](https://github.com/whitewhidow/bboink), [bb-portal](https://github.com/whitewhidow/bb-portal),
[hid-ble-poc](https://github.com/whitewhidow/hid-ble-poc)) are live examples of this infra in use.
