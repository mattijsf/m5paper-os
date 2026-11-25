# Decisions

Each entry records what was decided, why, and the rules that follow from it. Numbers are stable: code comments and other
docs refer to them (`D22`, `D39`), so an entry is never renumbered, merged or deleted. A decision that no longer holds is
marked **Superseded by Dn** and kept short for reference. Do not reopen a decision without new facts (a measurement, a
failed task); to change one, add a new entry that supersedes it.

## D1. PlatformIO with a `sim` (native) and a `papers3` (Arduino on pioarduino) environment

- **Decision**: one PlatformIO project builds the desktop simulator (`sim`) and the firmware (`papers3`).
- **Why**: one tool builds both targets and manages the ESP32 toolchain and library versions. The Arduino framework gives
  working Wi-Fi, TLS, HTTP, SD and LittleFS with little code, which matters more than the last bit of control. pioarduino
  `55.03.312-1` provides Arduino core 3.x on ESP-IDF 5.x; the official `espressif32` platform is stuck on core 2.x.
- **Rejected**: plain ESP-IDF (more control over power management, but much more glue code and a second build system for
  the simulator); Arduino IDE (no native target).

## D2. M5GFX and M5Unified as the hardware layer

- **Why**: they ship a tuned driver for this panel (4 waveform modes, a background update task), touch, RTC, power-off and
  touch-wake light sleep for the PaperS3, maintained by the board vendor. M5GFX sprites and fonts also compile natively, so
  the same drawing code runs in the simulator.
- **Rejected**: epdiy (touch, power and the simulator would be ours to build); an own panel driver (months of work, risk of
  panel damage). Where M5GFX is slow it is patched at build time instead (D24).

## D3. Own small UI kit instead of LVGL

- **Why**: LVGL assumes a fast display: animations, scrolling, frequent partial redraws. On e-ink each of those must be
  disabled or worked around, and per-region refresh-mode control needs hooks into its flush path anyway. PaperOS needs a
  dozen static widgets and paginated lists; a retained widget tree with explicit `invalidate(hint)` is a few thousand lines
  and gives exact control over what reaches the panel.
- **Cost**: layout and widgets are ours to write and maintain (`UI.md`).
- **Rejected**: LVGL 9 (above); immediate-mode UI (no way to know what changed without diffing everything; messy state).

## D4. Own simulator front end on SDL2, not M5GFX's `Panel_sdl`

- **Why**: the simulator needs what `Panel_sdl` does not do: headless PNG output without a window, e-ink emulation with a
  refresh log, scripted input, fake time. Shared code draws into an `LGFX_Sprite`; the simulator only shows that buffer.
- See `SIMULATOR.md`.

## D5. Full-screen 8-bit grayscale canvas, diffed against a shadow frame

- **Decision**: widgets draw into one 540x960 8-bit canvas; the DisplayManager diffs it against the last pushed frame.
- **Why**: 8 bits so anti-aliased glyphs blend correctly (4-bit palette sprites do not blend). The diff finds the minimal
  changed rectangles however sloppily widgets invalidate, and lets the manager choose a refresh mode per region. 518 KB +
  259 KB of 8 MB PSRAM is affordable (6.5 MB free measured after the canvas).
- **Rule**: nothing draws to `M5.Display` directly (no diff, no screenshots, a device-only path). See `DISPLAY.md`.

## D6. "Fast then settle" as the default refresh strategy

- **Decision**: a black/white Fast pass first, then a Text pass restores the grays once input stops.
- **Why**: measured on the device, Text mode takes 0.9-1.4 s and Fast 0.27-0.5 s. Fast-then-settle gives quick response and
  clean text without flashing. Ghosting is bounded by a budget that triggers a Quality clean during navigation. Details in
  `DISPLAY.md`.
- **Open**: revisit with custom waveforms (P7-1); a shorter Text LUT may make the Fast pass unnecessary for page turns.

## D7. Light sleep when idle, power-off when idle for long, no deep sleep

- **Why**: touch (GPIO 48) is not an RTC pin, so it can wake light sleep only (confirmed in the M5Unified source). Power-off
  through the PMS150G draws about 9 uA and wakes by button or RTC alarm, which beats deep sleep for long idle. E-ink keeps
  the image either way.
- Details and open measurements: `POWER.md`.

## D8. WhatsApp via Home Assistant phone-notification sensors

**Superseded by D16, then D22.** The idea: read WhatsApp through the Home Assistant companion app's notification sensors (no
WhatsApp client, no terms risk). It is not a fallback either: it puts message text in Home Assistant in plaintext, which D22
rules out.

## D9. Mail via the notification feed, full bodies later through a gateway

**Superseded by D17, then D22.** Proton Mail has no IMAP without Proton Mail Bridge, so the device cannot fetch mail directly.
The current path (D22) shows what the phone's mail notification shows.

## D10. Home Assistant over REST, not WebSocket

- **Why**: the device is asleep most of the time; a persistent WebSocket would keep Wi-Fi up (150 mA). REST
  request/response fits "wake, fetch, sleep". How states are read: D18.
- **Cost**: no live push; the Home app polls every 30 s while in active use.

## D11. Apps are compiled-in C++ classes with static registration

- **Decision**: `POS_REGISTER_APP`, a template folder (`src/apps/_template`) and `tools/new-app.sh`. Adding an app is a
  one-folder change. See `APPS.md`.
- **Why**: no scripting runtime, no dynamic loading, no extra RAM.
- **Rejected**: Lua/JS/MicroPython apps (interpreter size, RAM, slow text layout, a second API surface to document);
  loadable ELF modules (complex, fragile).

## D12. Fonts and icons as baked LVGL-format bitmap fonts

- **Decision**: `lv_font_conv` produces 4-bit anti-aliased fonts with kerning; M5GFX loads the format natively
  (`loadFont(data, ft_lvgl)` -> `BFFfont`, no LVGL needed). Icons come from Lucide's icon font and draw as glyphs through the
  same pipeline. No TTF rasteriser on the device.
- **Fonts**: Inter (UI), Literata (reading), JetBrains Mono (Teletekst, D32): all SIL OFL. **Icons**: Lucide (ISC).
- **Rules**: `src/gen/` is generated by `tools/gen-assets.sh`. `tools/gen_assets.py` works around an M5GFX loader bug by
  rewriting sparse cmap offsets payload-relative (`patch_cmap`). A new icon can shift the drawing of every icon (D41).
- **Rejected**: FreeType on the device (RAM, speed); VLW fonts (no kerning, bigger); PNG icons (a decode per draw).

## D13. EPUB parsed on the device, cached on the SD card

- **Why**: books must work with no server and no Wi-Fi. 8 MB PSRAM is plenty for zip inflate and one chapter. A one-time
  index per book (plain-text chapters and page offsets) makes later opens instant and page turns cheap.
- **Rejected**: converting books on a computer first (an extra step for every book).

## D14. Portrait 540x960 only

- **Why**: reading and lists fit portrait; one layout halves the UI work. Rotation may come later for the reader; a
  landscape two-column layout is not planned.

## D15. Secrets in `/data/secrets.json`, provisioned over USB serial

**Superseded by D19.** A long token cannot be typed on e-ink; serial provisioning was the first answer. Reading a file from
the SD card turned out simpler. Flash encryption stays off (it makes development and recovery harder; see `BACKEND.md`).

## D16. WhatsApp through a Home Assistant add-on

**Superseded by D22.** An unofficial Baileys-based WhatsApp add-on in Home Assistant stored recent messages in a template
sensor. Replaced because every Home Assistant integration can read that text, and because of the account ban risk of an
unofficial client. The add-on and integration are removed.

## D17. Proton Mail through Bridge and Home Assistant's IMAP integration

**Superseded by D22.** Proton Mail Bridge plus the core IMAP integration gave sender, subject and full text. Replaced because
it puts a decrypted mailbox and an IMAP password inside Home Assistant.

## D18. Home Assistant access: a dedicated non-admin user and per-entity reads

- **Decision**: the device signs in as its own non-admin Home Assistant user. States are read with `GET /api/states/<id>`
  over one kept-alive connection per refresh. Actions go through scripts and services.
- **Why**: other clients never get an admin token. `POST /api/template` (a one-call batch read) and firing events need
  admin, so they are not used.
- **Base URL**: `HA_URL=` in `/sd/paperos_config` (D19), normally the public HTTPS address so the device works away from
  home; a LAN `http://` address also works and saves the TLS cost. Never hard-coded or committed.
- **Consequence**: over TLS each connection costs a handshake (about 1.5 s, 45 KB RAM), so keeping the connection alive is
  required. If a tab takes more than 3 s (P5-2), add an aggregate template sensor on the Home Assistant side (`BACKEND.md`).

## D19. Secrets come from `/sd/paperos_config` (KEY=VALUE)

Supersedes D15.

- **Decision**: the device reads `/sd/paperos_config` at boot. Keys: `HA_TOKEN` (required for Home Assistant), `HA_URL`,
  `WIFI_SSID` / `WIFI_PASSWORD`, `WIFI_SSID_2` / `WIFI_PASSWORD_2` up to `_8`, `LICHESS_TOKEN` (D41). Details in
  `apps/settings.md`.
- **Why**: simpler than importing or serial provisioning, and the card is as readable as the internal flash anyway.
- **Rules**:
  - The device never writes, logs, prints or displays the values. To check a secret, report its key name and length only.
  - The value is everything after the first `=` (SSIDs may contain `/`, `.`, spaces, `=`). A network without a password key
    is an open network.
  - Networks added on the device live in `/data/wifi.json`. Known networks are card plus device; the card wins duplicates.
  - Connecting: the last network first with its stored BSSID and channel, else one scan and the strongest known network;
    never loop. The radio is 2.4 GHz only.

## D20. WhatsApp data in a hardened Home Assistant sensor

**Superseded by D22.** The D16 sensor and its events were excluded from the recorder and kept off dashboards. Replaced because
Home Assistant has no per-user entity permissions: any login and any integration can read the state anyway.

## D21. A text clipboard

- **Decision**: the OS has a text clipboard (`core/clipboard`, 4 KB, kept in `/data/clipboard.txt`). Every text field offers
  Paste / Copy all / Clear on a long press (the text-field sheet, `UI.md`). It is shared with the paired phone (D23).
- **Superseded part**: the WhatsApp send path through a Home Assistant script (`script.paperos_whatsapp_send`) is
  **superseded by D22** and removed, with the favourite chats and "New message". A reply now goes through the phone
  (`COMPANION.md` 7.2). Mail stays read-only.

## D22. Home Assistant never sees message text: the inbox comes from an end-to-end encrypted companion app

Supersedes D16, D17, D20 and the send path of D21.

- **Threat**: every integration runs inside Home Assistant Core with full access to all states and attributes, the event
  bus, `.storage` (config entries, passwords in plain JSON) and every service. A recorder exclusion does not help. So
  **message and mail text never reaches Home Assistant in plaintext**, and Home Assistant holds **no send rights or send
  rules**.
- **Decision**: a sideloaded Android app (`android/`, a NotificationListenerService) reads the WhatsApp and Proton Mail
  notifications, encrypts each one for the PaperOS device and posts only the ciphertext to Home Assistant. Home Assistant
  stores and forwards the blobs (trigger-based relay sensors filled through a script, kept out of the recorder): it can
  neither read nor forge an item. The device decrypts and shows the feed contract of `BACKEND.md` "Inbox feeds". Commands
  back (reply, mark read) are encrypted device to phone the same way. Protocol: `COMPANION.md`.
- **Crypto**: X25519, HKDF-SHA256, ChaCha20-Poly1305 (IETF), a counter per direction against replay. On the device
  libsodium, one code path for simulator and firmware, in `core`. On Android only platform APIs (`XDH`,
  `ChaCha20-Poly1305`, `HmacSHA256`, API 31+).
- **Pairing**: by QR code. The device shows its public key; both ends show a short fingerprint to compare.
- **Rules**:
  - Send limits (length, rate, one target) live in the phone app, not in Home Assistant.
  - Everything from Home Assistant is untrusted input: bounded JSON, sizes checked before decrypting.
  - A missing feed shows "Not set up yet" (`apps/inbox.md`).
- **Costs accepted**:
  - Mail shows only what the Proton Mail notification shows: sender and subject, maybe a short preview. No full body.
  - A WhatsApp reply works only while its notification is still on the phone (the RemoteInput action). No new message to
    an arbitrary chat. Muted chats and media are missing.
  - The phone must be on and online, and the app must not be killed by battery optimisation (foreground service plus an
    exemption).
  - A Kotlin app must be built, signed and maintained alongside the firmware.
  - Home Assistant still sees metadata (timing, sizes, count) and can drop or delay items.
- **Gains**: Home Assistant and its backups hold no message or mail text; no unofficial WhatsApp client on the account (no
  ban risk); no decrypted mailbox on the Home Assistant host; read sync for free (a notification dismissed on the phone can
  drop the item on the device); any app with notifications could follow at little cost.
- **Library choice**: libsodium over Monocypher or mbedTLS: the device's precompiled mbedTLS 3.6.6 has ChaCha20 and
  Poly1305 compiled out; libsodium 1.0.22 ships with the device libraries and in Homebrew. No webhook: the phone needs a
  token to read its queue anyway (`COMPANION.md` section 6).
- **Rejected**: keeping the add-ons and hardening Home Assistant further (the threat is any integration inside Core); D8
  (plaintext in Home Assistant); the device talking to the phone directly on the LAN for the feed (only works at home, the
  phone would need an open port).

## D23. The clipboard is shared with the paired phone

- **Decision**: the device clipboard (D21) and the phone's sync through the companion relay (D22) as a `clip` message in both
  directions (`COMPANION.md` 7).
  - Device to phone: every copy is sent at once (or when Wi-Fi is next up, within 24 hours) and replaces the phone's
    clipboard.
  - Phone to device: only on a user action (the share target "Send to Paper", the Quick Settings tile "Clipboard to
    Paper", the app's button), because Android 10+ lets an app read the clipboard only while it has focus. The device takes
    it on its next relay read; a text field's clipboard sheet and the reply screen's Paste read the relay first.
- **Switches**: Settings > Home Assistant > Phone "Share the clipboard" (config `phone.clipboard`, default on) and the app's
  "Share the clipboard with the Paper" (default on). Off on either side: that side sends nothing and drops a received `clip`.
- **Privacy rules**: the text travels end-to-end encrypted; Home Assistant sees only the envelope size. A masked field (a
  password) never offers Copy. A clipboard Android marks sensitive is never sent. A `clip` older than 24 hours is not
  applied. Neither side logs or stores the text beyond the clipboard itself.
- **Rejected**: reading the phone's clipboard in the background (Android does not allow it; an accessibility service or a
  keyboard app would, at far more access than this is worth); a "Send to phone" button instead of syncing every copy (a copy
  on the device is already a deliberate act).

## D24. M5GFX is patched at build time where it is slow

- **Why**: measured on the device (`DISPLAY.md` "Tap latency"), two M5GFX paths dominated the time from a tap to the panel
  driver having the update: per-pixel glyph drawing of BFF fonts (malloc, decode and a 1x1 fill per pixel) and the frame
  buffer write of a rotated panel (a 4-bit read-modify-write per pixel, column-wise through PSRAM).
- **Decision**:
  1. The glyph path is replaced in our own code (`ui/text.cpp` "Fast text path"): glyphs are rasterised once by LovyanGFX and
     cached. `tests/unit/text_fast_test.cpp` requires byte-identical output.
  2. Driver changes are applied by `tools/patch_m5gfx.py`, a pre-build script that edits `.pio/libdeps/<env>/M5GFX`. It is
     idempotent, fails the build when the library text is not what a patch was written for, and runs in every environment
     (`extra_scripts` under `[env]`).
- **Patches**:
  1. Tiled, table-driven frame buffer write for a rotated panel.
  2. The update task skips rows whose waveform finished.
  3. The patch 1 write runs on both cores (work-stealing counter; the helper runs below the EPD task, so waveform timing is
     untouched).
  4. Pixel pairs that already have their values are not driven again when the refresh mode changes (`DISPLAY.md`
     "Unchanged pixels are left alone"; `>set epd-keep off` restores the stock rule). The one patch that changes what the
     panel does, not only how fast.
  5. `BFFfont::drawChar` moves the pen by the rounded advance `updateFontMetric` reports. Baked fonts may store advances in
     sixteenths of a pixel (our Inter faces do); stock code measures them rounded (`(adv + 8) >> 4`) but draws them truncated
     (`>> 20`), so `textWidth` was about half a pixel per character wider than the drawn text. Rounding in the drawing is the
     spacing the font intends, and every layout already uses the measured width. This patch moves pixels and is needed by
     the simulator and unit tests too; a unit test fails on an unpatched library.
- **Rules**: patches 1-3 keep the driven pixels identical to stock; patch 4 only skips driving pixels that are already
  right; patch 5 moves glyph pixels (goldens with Inter text were re-accepted with it). After changing a patch: `rm -rf .pio/libdeps/papers3/M5GFX`, build, flash,
  `tools/device.py set epd-selftest on` and check that the log says "identical" (it compares patch 1 with the stock code).
- **Rejected**: vendoring M5GFX (150 MB, mostly fonts); a private-member hack (`#define private public`); an own panel
  driver (D2). ESP-IDF settings: D25.

## D25. The Arduino core and ESP-IDF are built from source with our own settings

- **Decision**: `custom_sdkconfig` in `platformio.ini` `[env:papers3]`. pioarduino downloads ESP-IDF 5.5.5, builds the Arduino
  core's ESP-IDF libraries with those settings (about 5 minutes after a change; run it in the background), copies them into
  the framework package and compiles the firmware against them. Generated files in the project root are git-ignored.
- **Why**: it opens settings the precompiled core fixes, with measured gains:
  - 64 KB data cache with 64-byte lines (was 32 KB / 32 B): a screen change during the previous settle waveform 408 -> 306
    ms (push call 169 -> 89 ms), internal heap free 105 -> 118 KB.
  - Memory settings of D27 and size settings of D31.
- **Required settings**: `CONFIG_SPIRAM_BOOT_HW_INIT` and `CONFIG_SPIRAM_BOOT_INIT`. `Os` (518 KB canvas and shadow) is a C++
  global; without them PSRAM is not up before global constructors and the firmware boot-loops in `DisplayManager`'s
  constructor.
- **120 MHz flash and PSRAM**: ESP-IDF 5.5 times 120 MHz octal PSRAM only with 120 MHz flash (no MSPI table for 80 MHz flash at
  a 240 MHz core clock). Experimental in ESP-IDF.
  - Set with `board_build.f_flash = 120000000L` (pioarduino derives the PSRAM speed from it and enables experimental features
    and flash HPM) and `board_build.f_image = 80000000L` (the image header: esptool takes no 120m for the ESP32-S3; ESP-IDF
    switches to 120 after boot).
  - `CONFIG_SPIRAM_TIMING_TUNING_POINT_VIA_TEMPERATURE_SENSOR` must stay off: on this chip its init returns
    `ESP_ERR_NOT_SUPPORTED` and the boot aborts.
  - Measured: PSRAM 51 -> 64 MB/s (`>set psram-test`); page turn 199 -> 140 ms, screen change 306 -> 190 ms, render 26 -> 12 ms
    (`tools/latency.py`). Soak tests (20 min over 2 MB with flash re-read and CRC-compared every pass; 10 min plus a page turn
    every 2 s): 0 errors, no reboot, chip at most 34 C.
  - **Temperature guard**: ESP-IDF warns about a drift of about 20 C, which the soak did not cover. The firmware restarts (so
    the boot tunes again) when the chip is more than 12 C from its boot temperature and nobody touched it for 30 s (`loop()`
    in `platform/device/main.cpp`; test with `>set temp-guard 1`). The boot log and every heartbeat show the temperature.
  - To go back: remove `board_build.f_flash` and `board_build.f_image`, delete `sdkconfig.papers3`, build.
- **Pitfalls**:
  - The second build phase runs under PlatformIO's own Python (`~/.platformio/penv`). It must have `platformio==6.2.0`:
    `6.2.0.post1` fails the platform's `>=6.2.0` check (a post release reads as older) and the platform uninstalls itself.
    Fix: `uv pip install --python ~/.platformio/penv/bin/python "platformio==6.2.0"`.
  - The rebuilt libraries replace those in PlatformIO's shared `framework-arduinoespressif32-libs` package; other projects on
    the same machine get them too.
  - ESP-IDF keeps an existing `sdkconfig.papers3` and applies `sdkconfig.defaults` only to options it does not decide:
    **delete `sdkconfig.papers3` after changing `custom_sdkconfig` or `board_build.f_*`**. The regeneration also wipes
    `.pio/build` while compile jobs run: build twice.
  - pioarduino ignores a `# ... is not set` line in `custom_sdkconfig`; write `CONFIG_X=n`.
- **Next candidates** (each measured before it stays): dropping unused components (Bluetooth, ESP-SR, RainMaker are still
  built), the FreeRTOS tick rate, `-O2` for ESP-IDF.

## D26. The word game is PaperOS's own, not a copy of woordle.nl

- **Decision**: build the genre (five letters, six tries, three marks, one word a day, statistics, a score to share), not
  the web game woordle.nl. Not taken from it: its list of daily words and their order (so not its puzzle of the day or its
  numbering) and its texts. The word game has its own word lists (`assets/wordgame/`), its own first day (2026-11-16 =
  puzzle 1) and its own order.
- **Why**: a game with the same rules is fine; another game's daily puzzle presented as that game is not.
- **Display name**: `wordgame::kName` in `wordgame.h`, currently "Woordle" (built as "Vijfje"). Title, launcher tile and
  shared score follow the constant; words, order and numbering stay the word game's own.
- **Fetched dictionary**: the app fetches the web game's word file about weekly to work offline, and keeps only its second
  part, the words accepted as a guess. The first part (the web game's daily words in order) is skipped by the parser and
  never stored. See `apps/wordgame.md` "Fetched words".
- **Cost**: the word of the day differs from the web game's, so scores do not compare with its players, and woordscore.nl
  does not file a shared score as the web game's. The share text keeps the genre's shape (`apps/wordgame.md`).
- **Names**: the app id is `wordgame` everywhere (directory, namespace, id, tests, docs). Data saved under the old id
  `vijfje` is adopted once (`apps/wordgame.md` "Names").

## D27. Ordinary allocations live in PSRAM; memory is measured per app

- **Why**: measured with `tools/memory.py` (reboots, opens each app, uses it; `>set memreport` logs internal free, lowest since
  boot, largest free block, PSRAM and stack high-water marks). Before: idle 108K internal free; Home during its TLS fetch 40K,
  **lowest 18K**; the word game lowest 14K with a 9K largest block, still 19K after leaving. Cause: ESP-IDF put every
  allocation under 4 KB (`SPIRAM_MALLOC_ALWAYSINTERNAL=4096`) in internal RAM, so strings, vectors, map nodes, chapter text
  and word lists competed with Wi-Fi and TLS, with no reserve (`SPIRAM_MALLOC_RESERVE_INTERNAL=0`).
- **Decision** (`custom_sdkconfig`, D25):
  - `SPIRAM_MALLOC_ALWAYSINTERNAL=0`: ordinary allocations go to PSRAM; FreeRTOS objects, task stacks and DMA buffers ask
    for internal RAM explicitly.
  - `SPIRAM_MALLOC_RESERVE_INTERNAL=32768`: kept for those.
  - `MBEDTLS_EXTERNAL_MEM_ALLOC`: TLS buffers in PSRAM.
  - Core dumps off (`ESP_COREDUMP_ENABLE_TO_NONE`; the `coredump` partition in `partitions.csv` stays unused).
  - The log ring (8 KB) and the serial upload buffer (4 KB) are heap blocks, so they live in PSRAM.
- **After**: idle 141K internal free; every app 126-141K with nothing kept after leaving; Home during TLS lowest 69K. Tap
  latency unchanged; EPD self-test identical.
- **Rules for code**:
  - Nothing to do per allocation.
  - A task that writes flash (the worker, the UI loop) keeps its stack in internal RAM: the cache is off during flash writes.
  - Large fixed buffers are heap blocks, not static arrays.
  - Arduino's HTTPClient spins with `delay(0)` around `NetworkClient::readBytes()`, which returns 0 at once while TLS has no
    data yet; `delay(0)` never lets IDLE0 run, so the task watchdog fired after 5 s during long HTTPS downloads.
    `platform/device/net.cpp` wraps the clients so a read that got nothing sleeps 1 ms. Keep that wrapper.
  - The worker's priority cannot be 0: `esp_pthread_set_cfg` rejects it and drops the whole config, core and stack included.
- **Open**: Bluetooth is linked but never started (small gain, risk to the core build); Wi-Fi buffer counts; the reader's
  prefetch canvas costs 486 KB of PSRAM (of about 5.5 MB free).

## D28. The agenda comes from the phone's calendar, one way, through the encrypted relay

- **Source**: the calendars the phone's calendar app shows (`CalendarContract.Instances`, `READ_CALENDAR`), read by the
  companion app, minus those switched off in the app under Paper > Calendars (the app stores the excluded ones, so a new
  calendar is on by default). Not a Home Assistant calendar entity: the calendar is as private as the messages, so D22's trust
  rule applies and Home Assistant only relays ciphertext.
- **One way**: the device shows, never changes. The phone sends `agenda` runs (`COMPANION.md` 7.1): today and the 13 days
  after it; a run replaces every day it covers; at most 12 events a day (the rest is a count); titles 80 and places 60
  characters; no notes, attendees or descriptions. Declined and cancelled events stay on the phone.
- **When**: a changed day's run a minute after the calendar changed; the whole window once a day and after pairing or
  switching it on. The device reads the relay as before (the Agenda app, the inbox, the scheduled refresh). Off by default on
  the phone ("Share the agenda"); switching it off, or revoking the permission, sends an empty run that clears the device.
- **Times** are the phone's wall clock; "now" and "today" on the device use its own clock and time zone.
- **On the device**: the Agenda app (Day: blocks on an hour grid; List: every event, paged), one line on the launcher, and
  the sleep screen (D35). The sleep screen can be hours old, so it names events with their times, never "in 5 minutes".

## D29. Weather comes from Home Assistant's Google Weather entities

- **Decision**: the device reads the Google Weather integration's entities in Home Assistant with the token it already has
  (D10). No Google API key on the card, no second host or TLS session; the API quota stays Home Assistant's.
- **Calls** (non-admin, D18; no `/api/template`), four per read over one kept-alive connection:
  - `GET /api/states/<entity>` for now;
  - `POST /api/services/weather/get_forecasts?return_response` with `"type":"daily"` and `"type":"hourly"` (the `forecast`
    attribute is gone since Home Assistant 2024.3);
  - `GET /api/states/sun.sun` for the next sunrise and sunset.
  Read only: the app never changes Home Assistant.
- **Places**: a place is a weather entity (the integration makes one per location); the device sends no coordinates or
  address anywhere. Up to four, `entity`..`entity4` and `name`..`name4` in `/data/apps/weather.json`; defaults in code are
  Thuis (`weather.thuis`) and Office (`weather.office`). A `Segmented` switcher above the pages; a saved copy and a read per
  place shown. A location is added in Home Assistant's UI (a config entry) and costs one more location of its quota.
- **Saved copy**: the parsed model (`/data/cache/weather/<entity>.json`, about 4 KB), not the raw answers (the hourly answer
  is about 10 KB). The saved copy shows first; a "now" older than 90 minutes gives way to the forecast for the current hour.
- **Reads**: when the app opens (if the last read is 15 minutes old), every 15 minutes while it stays open, and the refresh
  button. Nothing in the background or from the launcher; the sleep screen uses the saved copy (D35).
- **Pages** (`ui::Pager`, no scrolling): now with the next 16 hours and 4 days (5 without the switcher); today and tomorrow
  in blocks of three hours; all ten days. Blocks are clock-aligned (00, 03, ... local): the icon is the condition most of
  its hours have, the temperature their mean, the rain chance the highest, the amount the sum (`dayBlocks`, unit tested).
- **Rain graph**: the next two hours from a minute-forecast sensor (`sensor.google_neerslag_minuten`, a REST sensor in the
  Home Assistant config) as bars on the first page; hidden when older than 40 minutes. First place only: another place
  would need its own sensor.
- **Not used**: UV, pressure, visibility, gusts, dew point, weather alerts.

## D30. Files over Wi-Fi: WebDAV and an upload page while one Settings page is open

- **Where**: Settings > Storage > Files over Wi-Fi. The server runs only while that page is open, stops after 10 minutes
  without a request, and stops when the page is left. Wi-Fi and the device stay awake meanwhile (`NetService::keepAlive`,
  `Os::keepAwake`). Port 80 on the device, 8080 in the simulator.
- **Scope**: `/sd/books` and everything in it, nothing else. `..`, encoded or not, is refused. Names starting with a dot stay
  hidden and cannot be written (the reader's `.paperos` cache, uploads in progress). The Mac's `._name` and `.DS_Store`
  files are accepted and discarded.
- **Clients**: WebDAV class 1 and 2 (locks are faked: Finder mounts read-only without them), so Finder (Go > Connect to
  Server) and Android file managers can list, upload, download, rename, make folders and delete. A plain browser page does
  list, upload with progress, download, delete, New folder, Rename and Move (MKCOL and MOVE from the page's script), so a
  phone needs no WebDAV app. The reader finds books in folders and a moved book keeps its position (`apps/reader.md`).
- **Protection**: a new six-digit code per start, shown on the panel, as the HTTP password (any user name). Digest (macOS
  sends no Basic password over plain HTTP) and Basic (browsers). Ten wrong codes lock the server for a minute.
- **Limits of that protection**: plain HTTP on the home Wi-Fi. The files travel unencrypted, and the code does not stand up
  to a listener (Basic sends it as is; a captured Digest exchange falls to a try of all million codes). It keeps other
  devices out, nothing more; TLS would need a TLS server, which the firmware does not have (D31).
- **Implementation**: an HTTP/1.1 server on BSD sockets shared by the device (lwIP) and the simulator
  (`src/platform/posix/http_server`), because macOS sends uploads chunked and ESP-IDF's own server cannot read chunked
  bodies. The answers come from shared, unit-tested code (`core/file_server`). Uploads are written aside and renamed when
  complete.
- **SD write retries**: with Wi-Fi receiving, the card sometimes refuses a write (EIO; 5 of 6 one-megabyte uploads hit it).
  `fs::Writer` reopens the file and writes the piece again at its offset, up to three tries. Upload speed is about 70-100 KB/s
  (the card's write speed).
- **Wi-Fi power save** is off while serving (`hal::Net::setPowerSave`): downloads went from about 30 KB/s to 250-380 KB/s. It is
  switched back on when the server stops; a page destroyed without `onHide` leaves it off until Wi-Fi next restarts (Arduino
  keeps the setting).

## D31. Firmware size: no C++ exceptions, no iostreams, a trimmed ESP-IDF

The image went from 2,988,576 to 2,424,592 bytes (-564 KB, -19 %) with no visible change and the same tap latency. The app slot
is 5 MB (`app0`); the working budget is 3 MB. Compare the size `make fw` prints with `STATUS.md` after adding code.

| Step | Image after | Saved |
|---|---|---|
| `core/remote.cpp` parsed commands with `std::istringstream`; that one include linked libstdc++'s locale, wide-character and stream code. A 60-line word reader replaces it. | 2,900,960 | 88 KB |
| ESP-IDF: no C++ exceptions (no unwind tables, no unwinder), 44 common root certificates instead of 150, TLS client only, no SoftAP, no WPA Enterprise, no error-name table | 2,693,392 | 208 KB |
| Our code and the libraries compiled with `-fno-exceptions` (the Arduino build script still passed `-fexceptions`) | 2,526,512 | 167 KB |
| `CORE_DEBUG_LEVEL` 3 -> 1: the Arduino core and its libraries log errors only | 2,506,144 | 20 KB |
| ESP-IDF: no mbedTLS error texts, silent assertions | 2,424,592 | 82 KB |

(The two flash segments are aligned to 64 KB, so a step's saving can show partly in the next step.)

**Rules**:

- **No exceptions in firmware code.** `try`, `catch` and `throw` do not compile for the device. A standard-library throw
  (`std::stoi` on bad input, `at()` out of range, a failed allocation) is an `abort`. Parse with `strtol` / `from_chars` and
  check sizes.
- **No `<sstream>`, `<iostream>`, `<fstream>`, `<regex>` or `<locale>`** in code the firmware links. Each links 100-160 KB, and
  the simulator will not warn. `std::to_string`, `std::string`, containers and `<algorithm>` are fine.
- **TLS trusts 44 roots** (ESP-IDF's "common" list: Let's Encrypt, Google Trust Services, DigiCert, Amazon, Sectigo/Comodo,
  GoDaddy/Starfield, GlobalSign). A server under another root fails verification; to add one, switch back to the full bundle
  (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`, +47 KB).
- **No access point mode.** Arduino's WiFi library still asks for the AP network interface at start, so
  `platform/device/net.cpp` supplies an `esp_netif_create_default_wifi_ap` that returns none. A set-up hotspot would need
  `CONFIG_ESP_WIFI_SOFTAP_SUPPORT` back.
- **No TLS server.** Local servers (D30, D39) are plain HTTP or TCP.
- **Shorter crash output.** A failed assert prints its program address only, a TLS failure its number only. Decode with
  `xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/papers3/firmware.elf 0x42...` (`make monitor` does it automatically).

**Tried and dropped**: `CONFIG_LWIP_IPV6=n` (ESP-IDF's lwIP port and esp-modbus do not compile without it); `-Os` for our code
(already in effect: the build script's `-Os` came after our `-O2`).

**Not done, on purpose** (revisit only if the budget runs short; sizes from the linker map):

- Fonts and icons, 473 KB (11 text fonts at 4 bpp, two icon sizes): move to the card (the firmware then needs the card to
  show text), 2 bpp (about half, coarser edges), or drop Latin Extended-A (about a quarter less; ő, š, ł, œ become `?`).
- M5GFX and M5Unified drivers and detection for every M5Stack board (about 50 KB, removable by a patch).
- newlib's small printf (about 30 KB; loses `%llu` and friends, which the code uses).
- WPA3 (about 15 KB; WPA3-only networks would stop working).

## D32. Teletekst: the service's own JSON, drawn as a grid in ink and paper

- **Source**: `https://teletekst-data.nos.nl/json/<page>[-<subpage>]`, the JSON the NOS Teletekst site reads (25 lines of 40
  cells with colour classes, links and mosaics; `apps/teletekst.md` "Data"). Not a documented API: if its shape changes the
  app shows "Unexpected answer", and the parser (`teletekst_model`, unit tested) is the one place to change. Read straight
  from the device, not through Home Assistant: public pages, no key, and a request carries only the page number.
- **A grid, not reflowed text**: tables, sports results and the weather map only work in 40 columns. That needs a monospaced
  font: JetBrains Mono Medium 22 (SIL OFL), 13 px a character, 520 px a row, 21.5 KB. The screen takes 10 px side margins for
  it (`Screen::sideMargin()`). Rows are 26 px high, so mosaic pictures are a little tall.
- **Ink and paper only**: the television's black background is paper, dark backgrounds are ink bars with paper text, mosaic
  colours are solid or striped ink (`apps/teletekst.md` "Colours"), and characters are cut to ink or paper after drawing. A
  page is one Fast pass with nothing to settle (`Hint::Page`), like a reader page turn. Gray levels for the eight colours were
  rejected: flat gray on a screen that changes with every tap costs a second pass every time (`DISPLAY.md` "Gray areas cost a
  flicker").
- **Navigation without a remote**: tap a row to open its link (index pages are headlines ending in a page number); the four
  coloured links are four buttons; a number pad opens from the page number; swipes and arrows go through subpages (nothing
  rotates on a timer).
- **Reads**: only when a page is asked for (a copy under two minutes old is shown instead). The last 16 pages are kept in
  flash and shown first when the app opens and when a read fails. No background refresh, nothing on the launcher.
- **Test pages are made up** (`tests/fixtures/make-teletekst-fixtures.py`): the repository holds no text of the service.

## D33. Idle return: an unused app goes back to the launcher

**Superseded by D38.** A separate timer (`launcher.homeAfterSec`, default 60 s) sent an unused app back to the launcher. D38
replaces it with one idle time and a choice of what to show. Kept from D33: apps declare their own exemption (`.stayOpen`,
`Screen::staysOpen()`), and nothing leaves while the keyboard shows or a finger is down, so typed text is never lost.

## D34. Hacker News: order from the site's API, content from its search service

- **Two public services, no key**:
  - `hacker-news.firebaseio.com/v0` (the site's own API) for what only it knows: the site's order of a list's stories and of
    a story's direct comments, and a story's own text.
  - `hn.algolia.com/api/v1` (the site's search) for content in bulk: thirty stories in one answer, all comments of a story in
    one answer.
  The first alone is a request per item (31 for a list, 201 for a thread of 200 comments: seconds of radio per page); the
  second alone orders stories by points and comments by time, which is not the site's order. A list is two requests, a story
  two (`apps/hackernews.md` "Data").
- **Read straight from the device**, not through Home Assistant: public data, and a request carries only a list name or
  story ids.
- **Comments are pages of text, not a foldable tree**: the site's reading order, a reply 16 px further right per level (up to
  eight), "Next thread" to skip an exchange. Set in Inter 22 rather than the reader's Literata 24: half as much again on a
  page. `ui::TextBlock` gained a paragraph margin, a byline heading (level 4: bold at body size, kept with its text) and a
  real bold for the interface font.
- **The article through a reader service**: a tap on a story with a link shows the article as text, with its comments behind
  a button ("14 comments"); a long press opens the comments directly; a story without a link opens its comments.
  - Primary: `r.jina.ai` (Jina Reader, no key, 20 requests a minute) fetches the page and returns Markdown. Doing reader mode on
    the device would need an HTML parser and reader-mode rules in the firmware budget.
  - Fallback: `markdown.new` (Cloudflare, no key, 500 requests a day), asked only when Jina fails, finds no text or reports
    that the site refused it; of two texts the longer wins. On 30 front-page links each read 26, not the same ones; Jina's
    text is cleaner, so it stays first.
  - Tried and rejected: defuddle.md (rate-limited after ten requests), pure.md (paid), the Wayback Machine (stores the sites'
    refusals; Jina will not read it). Some news sites stay out of reach of every free service.
  - Privacy cost: every opened story with a link sends that link's address to Jina, and to Cloudflare when Jina fails;
    nothing else is sent with it. The feature lives as long as the services answer without a key. The link is also shown
    as a QR code on the article screen.
  - One code path for Hacker News and Read later (`hackernews::readPage`, D36).
- **Reopening**: the app is not exempt from the idle rule (D38); instead the open story comes back at its page when the app
  is opened again within the hour (`apps/hackernews.md` "Opened again").
- **Not included**: job adverts (the search does not know them), saved comments (20 to 300 KB a thread), login, votes,
  replies.
- **Test stories are made up** (`tests/fixtures/make-hackernews-fixtures.py`): the repository holds no text of the site.
- **New outside the app**: `ctx.transport()` (several requests in one worker job, `APPS.md`), `Paragraph::margin` and heading
  4 in `ui/textblock`, launcher tiles that fit a name wider than 137 px in the caption size, `Icon::Hackernews`. Speed fixes
  in `ui/` that every app gains: the pager's and the switcher's pressed state wait like a button's, text is measured from the
  glyph cache, the ellipsis cut is found by bisection (`DISPLAY.md` "Tap latency").

## D35. The sleep screen is a board of the day

- **Why**: the panel keeps its picture without power and the device is off most of the day. The date, the weather and the
  agenda make the device useful without a touch.
- **Only what stays true**: the picture stands for hours, so nothing on it is "now". Today's high and low and three-hour
  blocks instead of the current temperature; events with their times instead of "in 5 minutes"; the next days by name
  instead of "Tomorrow"; the date on top shows how old the picture is. No clock.
- **Freshness**: the scheduled refresh (off by default, `POWER.md`) also reads the weather, and the date follows midnight
  within its interval. With it off the board is as of the power-off.
- **Nothing is fetched at power-off** (no Wi-Fi join for a picture): the board uses the saved agenda and the weather app's
  saved copy; a copy older than a day is not shown.
- **Privacy**: the inbox stays a count per app (`BACKEND.md`); up to five event titles are shown. One switch removes the
  board: Settings > Power > "Today on the sleep screen".
- **Rejected**: Home Assistant states on the board (thermostat, doors, the car; P7-17). Not wanted; do not propose again.

## D36. Read later: a link shared from the phone becomes an article in the books folder

- **Why**: long reading turns up on the phone at the wrong moment, and the device is the screen for it. With the scheduled
  refresh on, the article is on the card before the device is picked up: no Wi-Fi wait, readable anywhere.
- **No new message type**: the share sheet's "Send to Paper" already sends a `clip` (D23). A `clip` that is one link (alone,
  or after a title, as browsers and news apps share it) is treated as a link to read. A separate `link` message with its own
  share target was rejected: it needs a new phone build for nothing visible. Side effect: the Quick Settings tile with a link
  on the phone's clipboard also makes an article (it can be removed).
- **An article is a file**: `/sd/books/Articles/<title> - <host>.md`, saved as the reader service returns it. The Read app
  opens `.md`, so an article gets everything a book has (typeface, text size, position, the sleep screen's row) with nothing
  of its own to keep in sync. It can be copied off the card, and any Markdown file put there opens too.
- **Reader service**: D34's (`r.jina.ai`, then `markdown.new`), with the same limits. The link's address leaves the device
  once, when the page is read.
- **Removing is for articles only** (long press, or "Remove" on the card): articles pile up by themselves; books came by
  hand and stay.
- **Badge**: the Read tile's badge counts what there is to read (links waiting, articles not opened). "N unread" on the
  launcher and the plain sleep screen stay messages.
- **When a page is read**: while the device is off (scheduled refresh) or when the library opens, never behind the launcher:
  a read takes seconds and there is one worker, so an app opened meanwhile would wait. With the scheduled refresh off, the
  first library visit after sharing waits for the page (a toast says so).

## D37. Signed firmware updates over Wi-Fi

The Settings page and its HTTP server are **replaced by D39** (a remote command on the wireless development port). The package
format, the signature checks and the rollback below still hold.

- **Push, not pull**: `make ota` builds, signs and sends from the development machine. A pull at the hourly wake was rejected:
  it needs a Home Assistant token on the development machine and that machine awake when the device wakes.
- **`make ota` never scans the network.** An early version probed every address on the LAN on port 80 (about 1000
  connections in seconds); the mesh router treated it as an attack, cut the running upload and kept dropping all traffic from
  that machine to the device's port 80, across device restarts. The tool takes the address from `HOST=`, from a device on USB
  (`device.py status`), or from the last update, and otherwise asks. Updates use their own port (8266).
- **Package**: the firmware image behind a 148-byte header (build, size, SHA-256, time), signed with Ed25519 (libsodium,
  already in the firmware for D22). The public key is compiled in (`src/core/update_key.h`, written by `make ota-key`). The
  private key is at `~/.config/paperos/ota-signing.key`, outside the repository; never print or commit it.
- **Checks**: the header signature before a byte is written, the image hash before it is made the boot image. Nobody else on
  the Wi-Fi can install firmware, whatever they capture. The image itself travels in the clear: it holds no secrets (those
  are on the card).
- **Not protected against**: replay of an older image signed with the same key (no version check, on purpose: going back to
  an earlier build is useful during development); someone who has the key file.
- **Two slots and rollback**: the image is written to the app slot that is not running (ESP-IDF OTA, sequential writes),
  checked, the boot slot switched, and the device restarts. Bootloader rollback is on (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`):
  a new image starts "pending" and is confirmed after it has run 15 s (`Os::loopOnce`, `kConfirmUpdateAfterMs`) or when the
  firmware itself switches the device off or restarts it. A crash, reset or power cut before that boots the previous image.
  The Arduino core would confirm at once; `verifyRollbackLater()` stops it. `make flash` over USB writes app0 and resets the
  slot choice, so USB always wins.
- **Build id**: `hal::buildId()`, the first 12 hex digits of the ELF SHA-256 in the image (`esp_app_get_elf_sha256`);
  `tools/ota.py` reads the same from `firmware.bin`. Settings > About and `tools/device.py status` show it; `make ota` sends
  nothing when the device already runs that build.
- **Measured**: 2.5 MB in 25 to 47 s (Wi-Fi sets the pace; flash is written a sector at a time from an internal-RAM buffer).
  A reset inside the 15 s brings the old slot back; a changed header gives 403, a changed image 400 after writing (never
  booted), a non-package 400.
- **Left out**: a version check; TLS (no TLS server, D31).

## D38. When unused: show the sleep screen or the launcher, staying on

Supersedes D33.

- **Why**: the device sits on USB much of the time, and on USB it never switches off (`kUsbGraceMs`, 12 hours after USB was
  last seen; `POWER.md`), so the Today board (D35) was never seen there.
- **Settings > Power**: "When unused for" 1 / 5 / 15 / 30 min / Never (`power.autoOffMin`; the reader keeps its own
  `autoOffMinReader`) and "Then show" **Home** | **Sleep screen** (`power.idleShow`, default Sleep screen). The old
  `launcher.homeAfterSec` in a config.json is ignored.
- **Sleep screen**: after the idle time the Shell draws the sleep screen (`Shell::showSleepScreen`) and the device stays on. Its
  footer says "Tap to start" instead of "Powered off". A tap returns to the launcher, or to the book or game that was open.
  While shown it is checked every 10 minutes and redrawn only when its content changed (`sleepScreenKey`: the date, an event
  that ended, a badge), so there is no flash for nothing. Every `power.refreshHours` it reads what a scheduled wake reads
  (relay, weather, articles) and shows "Updated HH:MM".
- **Home**: after the idle time an app that is not a book or game returns to the launcher.
- **On battery** the PowerPolicy switches off at the idle time as before (a device that stays on with a board would drain
  the battery); the picture is the same but for the footer.
- **Resume**: after an idle power-off the next start resumes only a book or a game (`prepareForIdleOff`), so an unused Weather
  or Mail does not come back hours later. An explicit power-off ("Sleep now", the launcher's button) resumes whatever was open.
- **Exemptions**: books and games (`.stayOpen = true` in the app registration). Nothing happens while the top screen returns
  `staysOpen()` (pairing, Files over Wi-Fi, the Display lab), the keyboard shows, or a finger is down.
- **Deep links**: a tap on a part of the sleep screen opens that app there: weather, an agenda row (that day), the book, an
  unread count. Apps receive the target through `AppContext::link` (key=value pairs); an app that does not know a key opens as
  usual. Anything else that opens an app at a place (a launcher line, a notification) uses the same link.

## D39. Wireless development: remote control and updates over Wi-Fi behind one switch

- **Same commands as USB** (`core/remote.h`): one protocol, one `Remote` class, a third transport (TCP, port 8266). The tools
  (`device.py`, `view.py`, `ota.py`) take `--wifi [--host ADDRESS]`; nothing else changes for them.
- **Updates are a remote command** (`>ota <size>`, paced by `#ack` like `>put`) instead of D37's HTTP page: one service, one
  port, one sign-in. Package and checks are D37's (`update::ImageWriter`); the command also works over USB, signed all the same.
- **Sign-in with the update key**, not a code on the panel (unlike D30): the key is already on the development machine,
  nothing to type, and a fresh nonce per connection means a listener cannot reuse a sign-in.
- **Not encrypted after sign-in**: someone on the home Wi-Fi who can hijack a TCP connection could inject commands. Accepted
  for a development switch that is off by default; the firmware has no TLS server (D31).
- **One switch**: Settings > Developer > Wireless development (`developer.wireless`, off by default). While on: Wi-Fi is up
  from boot (an exception to "no Wi-Fi at boot" in `POWER.md`), the device does not light-sleep, and it **does not switch
  itself off** (otherwise nobody can reach it until the side button). The idle sleep screen still appears, with the device on.
  This costs battery.
- **Developer page is a menu**: Wireless development and Status have their own pages; nothing on the menu redraws itself.
  Status has an Update button instead of a timer (a page that redraws itself counts its own refreshes).
- **Loop task stack 16 KB** (`SET_LOOP_TASK_STACK_SIZE` in `platform/device/main.cpp`): checking a valid Ed25519 signature
  overflowed the default 8 KB and restarted the board. No ESP-IDF rebuild needed.

## D40. Remote control from the companion app over the wireless development port

- **Same service as D39** (port 8266, `core/remote.h`): one more sign-in, no second server. The phone talks to the device
  directly on the home Wi-Fi; the relay through Home Assistant is far too slow for a screen and only tells the phone the
  device's address (`remote` message, `COMPANION.md` section 10).
- **Sign-in with a key from the pairing** (`HKDF-Expand(k_p2d, "PaperOS remote")`, HMAC over a fresh nonce), not the update
  key: the phone must never have that key. The pairing is already the trust between the two; unpairing ends remote control.
- **Allowed commands**: `>shot`, `>refresh`, `>tap`, `>long`, `>swipe`, `>ota`. A lost phone or an app bug cannot write files,
  read the log, change settings or restart the device. `>ota` only carries a package that must be signed with the update key
  (D37); the device checks that before writing a byte.
- **In the app**: Maintenance (Remote control, Firmware update); the app has no developer options. `make ota-phone` pushes the
  package to the phone's Downloads (adb); it is picked with Android's file picker.
- **Needs Wireless development on** (D39): while it is off the device listens to nobody. The phone says so when it cannot
  connect.
- **Not encrypted** after sign-in, as D39: frames cross the home Wi-Fi in the clear while the remote is open.

## D41. Chess, and a daily picture puzzle that was dropped

- **Pixel puzzle** (P7-22): a daily nonogram with pictures drawn for PaperOS, each proven solvable without guessing. Removed
  again after trying it; the code is in git history.
- **Chess** (P7-23, `apps/chess.md`): two opponents.
  - The Lichess computer (Stockfish on Lichess's servers), using the player's own account through a personal token
    `LICHESS_TOKEN` on the card (D19, read only at boot).
  - An offline game against a small engine on the device.
  Correspondence games against people are not included.
- **Polling, not streaming**: while it waits for Lichess's move the app asks `/api/account/playing` every 2.5 s instead of
  holding the Board API's game stream open: one answer per request like everything else on the device, Wi-Fi up only while
  waiting.
- **Launcher**: tile "Schaken" (icon `chess-knight`) after Hacker News; the other tiles keep their places.
- **Icons and the icon font**: a new icon can shift the drawing of every icon by a pixel (adding Lucide's `puzzle` changed 277
  screenshots; `chess-knight` changed none). Check the changed-screenshot count after adding one.
- **Pieces are drawn as shapes in code**, not icons: Lucide's chess icons are outlines (black and white pieces would look
  alike), and new glyphs can shift the icon font (above).
