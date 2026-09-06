# GOTCHAS

Hard-won lessons baked into this template. Read before you fight them again.

## BLE

- **Distinct MAC per firmware.** If two *different* firmwares can run on the *same*
  board (e.g. an OTA "switch"), the host caches GATT/pairing by MAC — writes then
  silently no-op on the second firmware. Fix (in `bleBegin`): derive the base MAC from
  the chip MAC XOR a per-firmware tag before NimBLE init. Chrome "forget" does **not**
  clear this; on Linux the real cache is BlueZ (`bluetoothctl remove <MAC>`).
- **`notify()` has no flush.** Two rapid `setValue()+notify()` calls **race** — the 2nd
  clobbers the 1st. Send **one** notification per response; pack fields with a separator
  (`ver:1.0.0|Board Name`), never two calls.
- **Control writes are open** (`WRITE | WRITE_NR`, no encryption) so a command lands even
  if bonding is stale. The portal auto-reloads after an OTA so it reconnects on fresh GATT.
- WiFi is **configure-only** in the portal — bringing WiFi up while BLE runs churns the
  radio on no-PSRAM boards. The reboot-to-fetch OTA connects WiFi itself, alone, at boot.
- **A notify bigger than the negotiated MTU silently fails on the C5.** The config JSON can
  exceed it, so it's served in small **chunks** (`__CFGGET__:<off>` → `cfg:<end>:<total>:<part>`),
  reassembled by the portal. A single oversized notify just vanishes with no error.
- **Web Bluetooth runs one GATT op at a time.** Overlapping `writeValue()` calls reject
  instantly with *"GATT operation already in progress"*, so the portal funnels every write
  through a **send-queue**.

## Boards / build

- **StampS3 (Cardputer) needs `board = m5stack-stamps3`.** `esp32-s3-devkitc-1` bootloops
  on it before `setup()`.
- **Buttonless boards + USB-HID.** `ARDUINO_USB_MODE=0` (TinyUSB HID) means no serial and
  no auto-reset → you can't re-enter download without a BOOT button. On a buttonless board
  keep `USB_MODE=1` (hardware CDC/JTAG) so esptool can auto-reset (this template's default).
- **Headless display must be a no-op**, not just skipped — initializing a panel that isn't
  wired **hangs** boot. See the `#if !APP_HAS_DISPLAY` path in `display.cpp`.
- **ESP32-C5** flashes with the bootloader at `0x2000` (not `0x0`), needs esptool ≥ 5, and
  its ROM often rejects the stub loader → `upload_flags = --no-stub`. The pioarduino C5
  build also hits a cold-cache `FRAMEWORK_DIR=None` transient — the CI retries 3×.
- **4 MB can't fit two OTA slots.** A 4 MB board (e.g. the Waveshare C5) ships a **single app
  slot — no A/B OTA**, so it can't self-update or firmware-switch; reflash it over USB/web-flasher.
  (This matches the switcher note below.) The 16 MB boards get real A/B slots.

## OTA / bins

- The **app-only** bin (`<repo>-app-<env>.bin`, published for OTA) is *not* flashable to a
  blank board — it's only the app partition. A blank board needs the **merged** bin
  (`<repo>-<env>.bin`: bootloader + partitions + app at `0x0`/`0x2000`), which the web
  flasher uses. Don't hand someone the app bin for a first flash.
- **Rollback is already on** in the stock Arduino SDK (`CONFIG_APP_ROLLBACK_ENABLE` + a
  9 s bootloader watchdog), and `initArduino()` auto-marks a booted app valid. So an OTA'd
  app that crashes early auto-reverts to the previous slot.
- **OTA URL uses `releases/latest`**, so the OTA can't resolve until you've cut a real
  release. A gitignored `src/dev_secrets.h` can define `APP_OTA_URL` to a self-hosted test
  bin while iterating (no CI build).

## Process

- **Never tag / cut a release without an explicit go.** Committing + pushing to `main`
  (incl. Pages deploys) is fine; `git tag vX.Y.Z` triggers the release build + flasher
  publish. Test first, tag on purpose.

## LittleFS partition must be named `spiffs`

Arduino's `LittleFS.begin()` looks for a partition **named** `spiffs` by default
(the subtype is also `spiffs` — LittleFS reuses it). The partition tables here name
the FS partition `spiffs` for exactly this reason, so `LittleFS.begin(true)` mounts
with no arguments. If you rename it (e.g. to `littlefs`), you must pass the label:
`LittleFS.begin(true, "/littlefs", 10, "littlefs")` — otherwise the mount silently
fails and nothing you write persists.

Also: read a file's size **after** closing the write handle. `File.size()` on a
still-open write handle can report 0, so guard commits (`rename`) on a non-zero size
to avoid clobbering a good file with an empty one — see `app.cpp` `__NOTEEND__`.

## Firmware switcher (hop between sibling apps over OTA)

The template ships a generic switcher: the portal's **3-tap on the board chip** →
"Switch firmware" list → the board reboots and flashes a sibling app's release bin
into the spare A/B slot (same reboot-to-fetch path as self-update; only the URL
differs). It's **off until you populate `src/switch_targets.h`** (empty by default →
the picker just says "no targets"). To join a mesh:

- Fill `SWITCH_TARGETS` per board with the siblings' `releases/latest/download/…-app-<env>.bin` URLs.
- Give **each** app a **distinct `APP_BLE_MAC_TAG`** (`board.h`) or the host serves a stale GATT cache after the hop.
- Every target must share this app's **chip + identical A/B partition table** (app slots at the same offsets) and publish an app-only bin for the matching env. 4MB single-app boards (no spare slot) can't switch — the fetch errors cleanly.
- **Data:** NVS config survives a switch; **LittleFS does not** (each app format-mounts its own FS). Sync/export anything important before switching.

## Relay (remote control over WiFi)

- **BLE + WiFi + TLS heap:** the **no-PSRAM C5** (Waveshare) can't hold all three at once — a TLS handshake fails with `SSL - Memory allocation failed (-32512)`. So going remote **drops BLE** there. **S3 boards keep BLE** even without PSRAM (enough SRAM); C5 keeps it only with PSRAM. `relayChipCanCoexist()` decides; `relaykeepble` forces keep.
- **One TLS at a time.** All HTTP lives in a single task (drain the reply queue, then pull) — never overlap a pull and a POST, or a second TLS context won't fit the no-PSRAM heap.
- **TLS keep-alive is the latency win.** A fresh handshake per request is ~500ms; reuse one persistent `WiFiClientSecure` (`setReuse(true)`) across pull/post. Plus **batching**: the relay returns all queued items per request (joined by `\x1e`), so a burst drains in one round-trip.
- **Don't drop queued commands.** Both FreeRTOS queues (cmd/reply) must **block** (`xQueueSend` with a timeout), not use a 0 timeout — a burst (rapid keys, chunked file) otherwise silently overflows and loses items.
- **Lowercase the URL scheme.** Mobile keyboards auto-capitalise the first letter → `Https://`, which a naive `startsWith("https")` reads as plain http → every request fails. Normalise on save AND load; set `autocapitalize=none` on the input.
- **Render free tier sleeps** (~15min idle → ~30-50s cold start). The portal pre-warms with `GET /health` before "Go remote"; the board's continuous long-poll keeps it warm during a session.

### Open-AP roaming

- **A captive-portal open AP poisons the DNS cache.** "Find open AP" scans open networks and keeps the first whose `GET /health` returns the relay's `ok` body (a captive portal returns a login page, so it's rejected). But a captive DNS resolves *every* host to its own login IP, and **that entry survives the fallback to real WiFi** — so every later TLS connect hits the wrong IP and fails with HTTPClient `-1` (looks like a dead relay; heap is fine). Fix: `dns_clear_cache()` (`lwip/dns.h`) after leaving a rejected AP / on fallback / on every connect; a hard WiFi cycle (`disconnect(true,true)+WIFI_OFF`) on fallback; probe `/health` on a **throwaway** `WiFiClientSecure` so a captive handshake can't wedge the persistent poll socket.
- **Green/blue means the relay actually answered**, not merely WiFi-up — confirmed by a fast `GET /health` probe on connect (a false "online" used to hide a dead link). A bad token / unreachable relay stays orange.
- **After a drop, retry the full find-open+creds cycle every 5 min** (ESP auto-reconnect covers a blip meanwhile); the *initial* connect retries fast (~8s) so a board that can't reach a network at boot keeps trying.

### Mailbox id

- **Derive the id from the factory eFuse MAC, not the BLE address.** NimBLE's address can be a rotating/resolvable private address that changes each boot, which would change the mailbox id every boot — so a saved id goes stale on an auto-boot (board polls one id, portal talks to another; "green but can't connect"). `ESP.getEfuseMac()` is stable and available before BLE init. A custom `relayid` overrides it.

### Chunked transfers must be PULL, not PUSH

- **Never push-stream a multi-chunk transfer over the relay.** If the board sends N chunks as N separate `/reply` POSTs, an occasional POST drops and you get a silent *partial* (BLE has no per-chunk POST so it's always fine). Use a **request/response per chunk** like the note editor: `__NOTEGET__:<off>` → one `note:<off>:<total>:<base64>` reply; the portal loops requesting each offset (wrap in a retry), so a dropped reply just re-requests that chunk. Base64 keeps data safe past the relay's `\x1e` record-separator batching.
