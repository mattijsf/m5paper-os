# Decisions

Each entry: what was decided, why, and what was rejected. Do not reopen a decision without new
facts (a measurement, a failed task). To change one, add a new entry that supersedes it.

## D1. PlatformIO with two environments: `sim` (native) and `papers3` (Arduino on pioarduino)

- **Why**: one tool builds both targets and manages the ESP32 toolchain and library versions.
  The Arduino framework gives working Wi-Fi, TLS, HTTP, SD and LittleFS with little code, which
  matters more than the last bit of control. pioarduino `55.03.312-1` provides Arduino core 3.x
  on ESP-IDF 5.x; the official `espressif32` platform is stuck on core 2.x.
- **Verified**: both environments build; firmware runs on the device (2026-11-15).
- **Rejected**: plain ESP-IDF (more control over power management, but much more glue code and
  a second build system for the simulator); Arduino IDE (no native target).

## D2. M5GFX + M5Unified as the hardware layer

- **Why**: they ship a tuned driver for this exact panel (4 waveform modes, background update
  task), touch, RTC, power-off and touch-wake light sleep for the PaperS3, maintained by the
  board vendor. M5GFX sprites and fonts also compile natively, so the same drawing code runs in
  the simulator.
- **Rejected**: epdiy (vendor docs mention it, but then touch, power and the simulator story are
  ours to build); writing a panel driver (months, risk of panel damage).

## D3. Own small UI kit instead of LVGL

- **Why**: LVGL assumes a fast display: animations, scrolling, frequent partial redraws. On
  e-ink each of those must be disabled or worked around, and refresh-mode control per region
  needs hooks into its flush path anyway. Our needs are a dozen static widgets and paginated
  lists; a retained widget tree with explicit `invalidate(hint)` is a few thousand lines and
  gives exact control over what reaches the panel.
- **Cost accepted**: we write layout and widgets ourselves (`UI.md`).
- **Rejected**: LVGL 9 (see above); immediate-mode UI (cannot know what changed without a diff
  of everything, and state handling gets messy).

## D4. Own simulator front end on SDL2, not M5GFX's `Panel_sdl`

- **Why**: we need things `Panel_sdl` does not do: headless PNG output without a window, an
  e-ink emulation with a refresh log, scripted input, fake time. Shared code draws into an
  `LGFX_Sprite`; the simulator only has to show that buffer.
- **Verified**: `program --headless --shot` works (2026-11-15).

## D5. Full-screen 8-bit grayscale canvas + diff against a shadow frame

- **Why**: 8 bits so anti-aliased glyphs blend correctly (4-bit palette sprites do not blend);
  a diff against the last pushed frame finds the minimal changed rectangles regardless of how
  sloppy the widgets invalidate, and lets the manager choose modes per region. 518 KB + 259 KB of
  8 MB PSRAM is affordable (6.5 MB free measured after the canvas).
- **Rejected**: drawing straight to `M5.Display` (no diff, no screenshots, device-only path).

## D6. "Fast then settle" as the default refresh strategy

- **Why**: measured on the device, Text mode takes 0.9-1.4 s and Fast 0.27-0.5 s. Showing a
  black/white Fast pass first and restoring grays with a Text pass once input stops gives quick
  response and clean text without flashing. Ghosting is bounded by a budget that triggers a
  Quality clean during navigation. Details: `DISPLAY.md`.
- **To revisit** in task P7-1 with custom waveforms (a shorter Text LUT may make the Fast pass
  unnecessary for page turns).

## D7. Light sleep is the idle state; power-off is the long-idle state; no deep sleep

- **Why**: touch (GPIO 48) is not an RTC pin, so it can wake light sleep only (confirmed in
  M5Unified source). Power-off through the PMS150G draws ~9 uA and wakes by button or RTC alarm,
  which is strictly better than deep sleep for long idle. E-ink keeps the image either way.
- Details and open measurements: `POWER.md`.

## D8. WhatsApp via phone notifications in Home Assistant, not a WhatsApp client  (superseded by D16; no longer a fallback since D22: it puts message text in Home Assistant)

- **Why**: WhatsApp has no API for reading personal messages. The alternatives are unofficial
  WhatsApp Web clients (Baileys, whatsapp-web.js, WAHA) running on a server: against WhatsApp's
  terms (account ban risk), they break when WhatsApp changes its protocol, and they need a
  linked-device session to be kept alive. The user already runs Home Assistant with the Android
  companion app on the phones; its notification sensors deliver exactly "recent unread messages"
  with no new service and no terms risk.
- **Limits accepted**: only messages that produced a notification (not muted chats), text only,
  no media, no history beyond 25 items.
- **Fallback** if the user finds this too limited: a Baileys-based gateway (Phase 8), after
  asking the user.

## D9. Mail via the same notification feed first; full bodies later through a gateway  (superseded by D17)

- **Why**: the user's address is Proton Mail, which has no IMAP without Proton Mail Bridge, so
  the device cannot fetch mail directly. Notifications give sender + subject today with zero
  infrastructure. Full text needs a home-network service (Bridge + IMAP to JSON): Phase 8, and
  only after the user confirms provider and host.
- **Open question for the user**: is sender + subject enough? Which mail provider(s)?

## D10. Home Assistant over REST, not WebSocket

- **Why**: the device is asleep most of the time; a persistent WebSocket would keep Wi-Fi up
  (150 mA). REST request/response fits "wake, fetch, sleep". How states are read: D18.
- **Cost accepted**: no live push; the Home app polls every 30 s while in active use.

## D11. Apps are compiled-in C++ classes with static registration

- **Why**: simplest thing that makes adding an app a one-folder change. No scripting runtime,
  no dynamic loading, no extra RAM. `POS_REGISTER_APP` + a template folder + `tools/new-app.sh`.
- **Rejected**: Lua/JS/MicroPython apps (interpreter size, RAM, slow text layout, second API
  surface to document); loadable ELF modules (complex, fragile).

## D12. Fonts and icons as baked LVGL-format bitmap fonts

- **Why**: `lv_font_conv` (Node, already installed) produces compact 4-bit anti-aliased fonts
  with kerning; M5GFX loads the format natively; Lucide ships as an icon font, so icons use the
  same pipeline and draw as glyphs. No TTF rasteriser on the device.
- **Fonts**: Inter (UI), Literata (reading): both SIL OFL. **Icons**: Lucide (ISC), the same
  set the user's Home Assistant dashboard uses.
- **Verified (P1-6)**: M5GFX 0.3.0 loads `lv_font_conv --format bin` output natively on both
  platforms (`loadFont(data, ft_lvgl)` -> `BFFfont`, no LVGL needed). One loader bug is worked
  around in `tools/gen_assets.py`: sparse cmap offsets are rewritten payload-relative (see
  `patch_cmap`). Blobs: 13 fonts, 455 KB; firmware 1.35 MB.
- **Rejected**: FreeType on device (RAM, speed); VLW fonts (no kerning, bigger); bitmap icons
  as PNG (one decode per draw).

## D13. EPUB parsed on the device, cached on the SD card

- **Why**: books must work with no server and no Wi-Fi. 8 MB PSRAM is plenty for zip inflate
  and one chapter. A one-time index per book (plain-text chapters + page offsets) makes later
  opens instant and page turns cheap.
- **Rejected**: converting books on a computer first (extra step for every book).

## D14. Portrait 540x960 only

- **Why**: reading and lists fit portrait; one layout halves the UI work. Rotation can be added
  later for the reader (landscape two-column is not planned).

## D15. Secrets in `/data/secrets.json`, provisioned over USB serial  (superseded by D19)

- **Why**: a 180-character token cannot be typed on e-ink. Serial provisioning from a
  gitignored `config.local.json` is one command. Flash encryption is not enabled (it makes
  development and recovery harder); the trade-off is stated in `BACKEND.md`.

## D16. WhatsApp through a Home Assistant add-on. Supersedes D8  (superseded by D22; add-on removed 2026-11-16)

- **Decision**: the user declined the phone-notification route and asked for a Home Assistant
  plugin that reads the messages and stores them, so the device only reads a small list.
  Chosen: "WhatsApp for Home Assistant" by FaserF (add-on + `whatsapp` integration, Baileys
  based, links to the account like WhatsApp Web). It fires `whatsapp_message_received`; a
  trigger-based template sensor keeps the recent items (`ha/paperos_inbox.yaml`).
- **Why this one**: actively documented, runs as an HA OS add-on, exposes incoming messages as
  events with sender name and text, also reports the user's own replies (used to drop answered
  chats). Alternatives seen: WAHA add-on / `ha-waha` (alpha, text only), older Baileys add-on
  forks (`new_whatsapp_message` event, unclear maintenance), Whinself (webhook based).
- **Risk the user accepted**: unofficial client on the personal account; the add-on's own docs
  warn of possible bans. PaperOS use is read-only. Checks and the warning to repeat at install
  time are in `BACKEND.md`. D8 stays documented as the zero-risk fallback: same sensors, same
  device code, only the triggers in the package change.
- **Not verified**: event fields for groups, effect on phone notifications. Task P6-1.

## D17. Proton Mail through Bridge + Home Assistant's IMAP integration. Supersedes D9  (superseded by D22; Bridge add-on removed 2026-11-16)

- **Decision**: Proton Mail
  Bridge runs where Home Assistant can reach it; the core IMAP integration fires `imap_content`
  per new mail; a template sensor keeps the newest 20 with a 1500-character excerpt; the full
  text is fetched on demand through the `imap.fetch` action. No custom gateway.
- **Why**: uses a core integration for the mail logic (MIME, IDLE push); only Bridge is extra.
- **Open**: how to run Bridge on HA OS. The known community add-on documents itself as
  send-only; a small local add-on wrapping the `protonmail-bridge` image is the likely answer.
  Needs a paid Proton plan and an interactive login with 2FA. Task P6-2.
- **Rejected**: Proton notification text only (no body); `hydroxide` (its IMAP is marked
  work-in-progress); a separate gateway service (more to host than a Bridge add-on).

## D18. Home Assistant access: dedicated non-admin user, public HTTPS URL (LAN optional), per-entity reads

- **Why**: the home-assistant repo's standing rule is that other devices never get the admin
  token (the TV app has its own non-admin user). `POST /api/template` (one-call batch read)
  and firing events need admin, so they are not used: states are read with
  `GET /api/states/<id>` over a kept-alive connection, and "mark all read" goes through a
  script service. The LAN `http://` address avoids TLS cost on the device.
- **To measure** in P5-2: if a tab takes more than 3 s, add an aggregate template sensor on the
  Home Assistant side (BACKEND.md).

- **2026-11-15 note**: the base URL is the **public HTTPS address**, set as `HA_URL=` in `/sd/paperos_config`, so the
  device works away from home too; never hard-coded or committed. This replaces "LAN `http://`" above as the
  expected case (LAN stays an option). Consequence: per-entity reads over TLS cost a handshake (~1.5 s, ~45 KB
  RAM) per connection, so one kept-alive connection per refresh is a must, and the aggregate template sensor
  of P8-2 becomes more likely to be needed: **measure in P5-2** (tab load time over HTTPS, cold and warm).

## D19. Secrets come from `/sd/paperos_config` (KEY=VALUE). Supersedes D15

- **Why**: the owner put the Home Assistant token there (`HA_TOKEN=`, 183 characters, checked on
  the device by key name and length only). Reading the file at boot is simpler than importing
  it or provisioning over serial, and the card is as readable as the internal flash anyway.
- The device never writes, logs or displays the values. Optional keys: `HA_URL`, `WIFI_SSID`,
  `WIFI_PASSWORD`. Details: `apps/settings.md`.
- **2026-11-15 addendum: several Wi-Fi networks.** `WIFI_SSID`/`WIFI_PASSWORD` is the first network,
  `WIFI_SSID_2`/`WIFI_PASSWORD_2` ... `_8` more; no password key = open network; the value is everything after the first
  `=` (SSIDs may contain `/`, `.`, spaces, `=`). Device-added networks live in `/data/wifi.json`; known networks = card +
  device, the card wins duplicates. Connect: last network first with its stored BSSID/channel, else one scan and the
  strongest known network; never loop. The radio is 2.4 GHz only. The card on the device holds `HA_TOKEN`, `WIFI_SSID`,
  `WIFI_PASSWORD`, `HA_URL` (names and lengths only, checked 2026-11-15).

## D20. WhatsApp data lives in a hardened Home Assistant sensor  (superseded by D22)

- **Decision**: the WhatsApp feed stays a Home Assistant template sensor (`sensor.paperos_whatsapp_inbox`, D16), hardened: the sensor and
  the `whatsapp_message_received` / `whatsapp_message_sent` events are excluded from the recorder (no history, no logbook) and the sensor is
  on no dashboard.
- **Why / accepted risk**: Home Assistant has no per-user entity permissions: any login can read an entity's state by name. The user accepts
  that for this one sensor.
- **Rejected**: the device reading the WhatsApp add-on directly on the home network (its chats API on port 8066): it would only work at home, and
  the device would need the add-on's API key on the SD card. The add-on's API token belongs only in the Home Assistant integration; the device
  needs only `HA_TOKEN` and `HA_URL`.
- Related: the add-on's open endpoints (accepted risk, BACKEND.md "Add-on security"), D8 stays the fallback.


## D21. A clipboard, and WhatsApp can send. Changes D16 "read-only"  (send path superseded by D22; the clipboard stays)

- **Decision**: the OS gets a text clipboard (`core/clipboard`: 4 KB, kept in `/data/clipboard.txt`), every text field offers
  Paste / Copy all / Clear on a long press, and the WhatsApp app can send a message (reply in a chat, or to a favourite chat from
  the config), also by pasting the clipboard. Main use: sending game scores to one group chat.
- **How**: the device calls a Home Assistant script (`script.paperos_whatsapp_send`), never the add-on itself; the script holds the
  guards (length, target form, rate limit). A send always asks for confirmation on the device first.
- **Risk**: sending through the unofficial add-on raises the ban risk the user accepted for reading (D16). Keep the volume
  human-like: the rate limit in the script is part of the decision, not a detail.
- **Status (2026-11-16)**: built in the simulator: `core/clipboard`, the text-field sheet (UI.md), `script.paperos_whatsapp_send` in
  `ha/paperos_inbox.yaml` (guards in BACKEND.md "Sending a WhatsApp message"), Reply / New message / compose / confirmation in the WhatsApp
  app and `apps/inbox/compose.h` for other apps (apps/inbox.md "Sending"). The compose screen and the sheet ran on the device (frame dumps).
  **Superseded (2026-11-16)**: the script was installed, sent one message to the user's own number, and was removed the same day with
  the WhatsApp integration (D22). Favourites, "New message" and the script are gone; the clipboard and the text-field sheet stay, and
  a reply now goes through the phone (D22, COMPANION.md 7.2).
- Mail stays read-only.

## D22. Home Assistant is not trusted with message text: the inbox comes from an end-to-end encrypted companion app. Supersedes D16, D17, D20 and the send path of D21

- **Rule**: "I do not trust HA fully, too many integrations that might go rogue". Every integration runs inside
  Home Assistant Core with full access to all states and attributes, the event bus, `.storage` (config entries, an IMAP password in
  plain JSON) and every service; a recorder exclusion does not help against that. So **message and mail text never reaches Home
  Assistant in plaintext**, and Home Assistant holds **no send rights or send rules**: D16/D17 failed this (a rogue integration could
  read every WhatsApp message and, through Bridge, the whole Proton mailbox, and could call `whatsapp.send_message` past the guards
  of `script.paperos_whatsapp_send`).
- **Decision**: a small sideloaded Android app on the user's phone (NotificationListenerService) reads the WhatsApp and Proton Mail
  notifications, encrypts each one for the PaperOS device (authenticated: X25519, HKDF-SHA256, ChaCha20-Poly1305, a counter per
  direction against replay) and posts only the ciphertext to Home Assistant. Home Assistant stores and forwards the blobs (trigger-based
  relay sensors filled through a script, kept out of the recorder) and is a dumb pipe: it can neither read nor forge an item. The
  device decrypts (libsodium, one code path for simulator and firmware, in `core`), then shows the same feed contract as today
  (BACKEND.md "Inbox feeds"). Pairing by QR code (the device shows its public key; both ends show a short fingerprint the user
  compares). Commands back (reply, mark read) are encrypted device to phone the same way. **Send limits (length, rate, one target)
  live in the phone app**, not in a Home Assistant script. The exact protocol: `docs/COMPANION.md`; tasks: ROADMAP.md Phase 6.
- **2026-11-16 addendum (protocol v1)**: libsodium instead of Monocypher or mbedTLS (the device's precompiled mbedTLS 3.6.6 has
  ChaCha20 and Poly1305 compiled out; libsodium 1.0.22 ships with the device libraries and in Homebrew); ChaCha20-Poly1305 IETF and
  HKDF-SHA256 so Android needs no library (`XDH`, `ChaCha20-Poly1305`, `HmacSHA256` are in the platform from API 31); no webhook (the
  phone needs a token to read its queue anyway, COMPANION.md section 6).
- **Costs the user accepts**:
  - **Mail** shows only what the Proton Mail notification shows: sender and subject, maybe a short preview. No full body, no
    full-text fetch (D17 had rejected "notification text only" for this reason).
  - **WhatsApp replies only while that notification is still on the phone** (its RemoteInput action). No "new message to a
    favourite chat" (Somdoku's "Stuur naar WhatsApp" loses its path until a reply-based or phone-side alternative exists). Muted chats
    and media are missing.
  - **The phone must be on and online** (and the app not killed by battery optimisation: foreground service plus an exemption).
  - **A sideloaded Kotlin app must be built, signed and maintained**, alongside the firmware.
  - Home Assistant still sees metadata (timing, sizes, count) and can drop or delay items. The device treats everything from Home
    Assistant as untrusted input (bounded JSON, sizes checked before decrypting).
- **Gains**: Home Assistant, the Green and its backups hold no message or mail text; no unofficial WhatsApp client on the account (ban
  risk gone); no decrypted Proton mailbox on the Green; read sync for free (a notification dismissed on the phone can drop the item
  on the device); any app with notifications (Signal, SMS, calendar) could follow at little cost.
- **Done on 2026-11-16**: both feeds emptied first; the WhatsApp Gateway session logged out
  (the add-on's "Hard Reset & Logout": Baileys `logout()`, credentials deleted); the `whatsapp` integration, the WhatsApp Gateway
  add-on (with its data), the FaserF add-on repository and the local Proton Mail Bridge add-on (with its data) removed; the package
  cut down to `script.paperos_inbox_clear` and a recorder exclusion (home-assistant commit `ebc133d`), the orphaned entities removed.
  The device shows "Not set up yet" for a missing feed (apps/inbox.md).
- **Open (questions for the user, STATUS.md)**: WhatsApp reply scope, whether sender + subject is enough for mail, which phone and
  Android version, whether the phone is always on and online at home. Nothing of the app or the crypto is built before the answers.
- **Rejected**: keeping the add-ons and hardening Home Assistant further (the threat is any integration inside Core); D8 (Home
  Assistant companion app notification sensors: plaintext in Home Assistant); the device talking to the phone directly on the LAN
  (only at home, the phone would need an open port).

## D23. The clipboard is shared with the paired phone

- **Decision**: the Paper's clipboard (D21) and the phone's sync through the companion relay (D22) as a new message `clip` in both
  directions (COMPANION.md 7). Paper to phone: every copy on the Paper goes to the phone at once (if Wi-Fi is not reachable, when it is next up, within 24 hours) and replaces the
  phone's clipboard. Phone to Paper: only on a user action (the share sheet target "Send to Paper", the Quick Settings tile
  "Clipboard to Paper", the app's button), because Android 10+ lets an app read the clipboard only while it has focus; the Paper
  takes it on its next read of the relay, and a text field's clipboard sheet and the reply screen's Paste read the relay first.
- **Switches**: Settings > Home Assistant > Phone "Share the clipboard" (config `phone.clipboard`, default on) and the app's
  "Share the clipboard with the Paper" (default on). Off on either side: nothing is sent from that side and a received `clip` is
  dropped.
- **Privacy**: the text travels end-to-end encrypted like a notification; Home Assistant sees only an envelope's size. A masked field
  (a password) never offers Copy, a clipboard Android marks sensitive is never sent, a `clip` older than 24 hours is not applied, and
  neither side logs or stores the text beyond the clipboard itself.
- **Rejected**: reading the phone's clipboard in the background (Android does not allow it; an accessibility service or a keyboard
  app would, and both are far more access than this is worth); a "Send to phone" button instead of sending every copy (the owner asked
  for sync; a copy is already a deliberate act on the Paper).

## D24. M5GFX is patched at build time where it is slow; pixels stay identical

- **Why**: measured on the device (DISPLAY.md "Tap latency"), two M5GFX paths dominated the time from a tap to the panel driver
  having the update: the per-pixel glyph drawing of BFF fonts (malloc + decode + a 1x1 fill per pixel) and the frame buffer write
  of a rotated panel (one 4-bit read-modify-write per pixel, column-wise through PSRAM).
- **Decision**: (1) the glyph path is replaced in our own code (`ui/text.cpp` "Fast text path"): glyphs are rasterised once by
  LovyanGFX itself and cached; `tests/unit/text_fast_test.cpp` requires byte-identical output to LovyanGFX. (2) Driver changes are
  applied by `tools/patch_m5gfx.py`, a PlatformIO pre-build script for the device that edits `.pio/libdeps/papers3/M5GFX`
  (idempotent; it fails the build when the library text is not what the patch was written for). Patch 1: tiled, table-driven
  frame buffer write for a rotated panel. Patch 2: the update task skips rows whose waveform finished. Patch 3: the
  write of patch 1 runs on both cores (work-stealing counter; the helper runs below the EPD task, so waveform timing is untouched).
  Patch 4 (2026-11-17, the one patch that changes what the panel does, not only how fast): pixel pairs that already have
  their values are not driven again when the refresh mode changes (DISPLAY.md "Unchanged pixels are left alone";
  `>set epd-keep off` is the stock rule). `>set epd-selftest on`
  compares patch 1 with the stock code on the device (identical in Fast and Quality, 2026-11-16).
- **Rejected**: vendoring M5GFX (150 MB, mostly fonts); a private-member hack (`#define private public`); our own panel driver (D2).
- **ESP-IDF settings**: see D25 (the core is now built from source).

## D25. The Arduino core and ESP-IDF are built from source with our own settings

- **How**: `custom_sdkconfig` in `platformio.ini` [env:papers3]. pioarduino then downloads ESP-IDF 5.5.5, builds the Arduino core's
  ESP-IDF libraries with those settings (about 5 minutes after a change), copies them into the Arduino framework package and
  compiles the firmware against them. Generated files in the project root are ignored (`.gitignore`).
- **Settings now**: 64 KB data cache with 64-byte lines (was 32 KB / 32 B). Measured: a screen change during the previous one's
  settle waveform 408 -> 306 ms (push call 169 -> 89 ms), the EPD self-test write 30 -> 28 ms, internal heap free 105 -> 118 KB.
  `CONFIG_SPIRAM_BOOT_HW_INIT` / `CONFIG_SPIRAM_BOOT_INIT` are required: our `Os` (518 KB canvas and shadow) is a C++ global, and
  without them PSRAM is not up before global constructors (the first rebuilt image boot-looped in `DisplayManager`'s constructor).
- **Pitfalls hit**: (1) the second build phase runs under PlatformIO's own Python (`~/.platformio/penv`); its `6.2.0.post1` failed
  the platform's `>=6.2.0` check (a post release reads as older) and the platform uninstalled itself. Fix: `uv pip install --python
  ~/.platformio/penv/bin/python "platformio==6.2.0"`. (2) The rebuilt libraries replace the ones in the shared package
  `~/.platformio/packages/framework-arduinoespressif32-libs` (other projects on this machine using it get them too).
- **120 MHz flash and PSRAM (in use since 2026-11-16, the owner chose to try it)**: ESP-IDF 5.5 times 120 MHz octal PSRAM only with
  120 MHz flash (no MSPI table for 80 MHz flash at a 240 MHz core clock). Set with `board_build.f_flash = 120000000L` (pioarduino
  derives the PSRAM speed from it and enables experimental features and flash HPM) and `board_build.f_image = 80000000L` (the image
  header: esptool takes no 120m for the ESP32-S3; ESP-IDF switches to 120 after boot). `CONFIG_SPIRAM_TIMING_TUNING_POINT_VIA_
  TEMPERATURE_SENSOR` must stay off: on this chip its init returns ESP_ERR_NOT_SUPPORTED and the boot aborts.
  - **Measured**: PSRAM 51 -> 64 MB/s (`>set psram-test`); page turn 199 -> 140 ms, screen change 306 -> 190 ms, small tap 6 -> 5 ms
    (`tools/latency.py`; flash at 120 MHz also speeds code and fonts: render 26 -> 12 ms). Boot memory test passes.
  - **Stability**: 20 min soak (8429 passes over 2 MB, ~34 GB checked, 512 KB flash chunks re-read and CRC-compared every pass)
    and 10 min soak + a page turn every 2 s: 0 errors, no reboot; chip 29.4 C at boot, at most 34.4 C. Not covered: the ~20 C drift
    ESP-IDF warns about. **Guard**: the firmware restarts (the boot tunes again) when the chip is more than 12 C from its boot
    temperature and nobody touched it for 30 s (`loop()` in `platform/device/main.cpp`; tested with `>set temp-guard 1`). The boot
    log and every heartbeat show the temperature.
  - **Before the change** the whole flash was saved to `backup/before-120m-2026-11-16.bin` (git-ignored: holds /data). To go back:
    remove `board_build.f_flash` and `board_build.f_image`, delete `sdkconfig.papers3`, build.
  - **Pitfalls**: ESP-IDF keeps an existing `sdkconfig.papers3` and applies `sdkconfig.defaults` only to options it does not
    decide: **delete `sdkconfig.papers3` after changing `custom_sdkconfig` or the board frequencies**. That regeneration also
    wipes `.pio/build` while compile jobs run: build twice. pioarduino ignores a "# ... is not set" line in `custom_sdkconfig`;
    write `CONFIG_X=n`.
- **Next candidates** (each measured before it stays): dropping
  unused components (Bluetooth, ESP-SR, RainMaker are built today) for flash and RAM, FreeRTOS tick rate, `-O2` for ESP-IDF.

## D26. The word game is PaperOS's own, not a copy of woordle.nl (2026-11-16)

- **Asked**: "Create woordle game" with a handover text of that web game: its word list address, its day formula (start
  7 January 2022, number = days + 202), its texts, colours and share message; the user dropped colours and high contrast and
  wanted the clipboard text to stay the same.
- **Decision**: build the genre (five letters, six tries, three marks, one word a day, statistics, a score to share), not
  that game. Left out: its name, its list of daily words and their order (its puzzle and number of the day), its texts. The word game
  has its own name ("Vijfje" as built), its own word lists (`assets/wordgame/`), its own first day (2026-11-16 = puzzle 1) and order.
- **Why**: the assistant building it does not recreate a specific existing game (name, levels, look); woordle.nl is not the
  user's game (apps/somdoku.md said so already). A word game with the same rules is fine, the other game's daily puzzle
  under its name is not.
- **Cost**: the word of the day differs from the web game's, so scores cannot be compared with people who play that, and
  woordscore.nl does not read a score under its own name as the web game's. The share text keeps the genre's shape (apps/wordgame.md).
- **Also found**: the handover's word list address answered 10 + 30 words on 2026-11-16, not 844 + 5616 (filled later that day).
- **Name, later the same day**: the user changed `kName` to "Woordle" by hand (their decision, against "own name" above; the
  assistant declined to make the rename). Title, launcher tile and shared score follow the constant. Words, order and
  numbering stay the word game's own.
- **Later the same day**: the app fetches that file and uses its second
  part only, the words accepted as a guess (a dictionary); the first part, the web game's words of the day in order, is
  skipped by the parser and never stored. The line of this decision stays where it was.

## D27. Memory: ordinary allocations live in PSRAM; measured per app

- **Measured** (`tools/memory.py`: reboots, opens each app, uses it, `>set memreport` logs internal free / lowest since boot /
  largest free block, PSRAM, task stack high-water marks). Before: idle 108K internal free; the reader 69K (lowest 48K);
  Home during its TLS fetch 40K, **lowest 18K**; Woordle 18K (lowest 14K, largest block 9K) and still 19K after leaving.
- **Cause**: ESP-IDF put every allocation under 4 KB (`SPIRAM_MALLOC_ALWAYSINTERNAL=4096`) in internal RAM: strings, vectors,
  map nodes, chapter text, word lists competed with Wi-Fi and TLS, with no reserve (`SPIRAM_MALLOC_RESERVE_INTERNAL=0`).
- **Decision** (`custom_sdkconfig`, D25 build): `SPIRAM_MALLOC_ALWAYSINTERNAL=0` (ordinary allocations to PSRAM; FreeRTOS
  objects, task stacks and DMA buffers ask for internal RAM explicitly), `SPIRAM_MALLOC_RESERVE_INTERNAL=32768` (kept for those),
  `MBEDTLS_EXTERNAL_MEM_ALLOC` (TLS buffers in PSRAM), core dumps off (there is no partition for them; every boot logged a failed
  write). In code: the log ring (8 KB) and the serial upload buffer (4 KB) are heap blocks now, so they live in PSRAM.
- **After**: idle 141K internal free; every app 126-141K, nothing kept after leaving; Home during TLS 102K, **lowest 69K**.
  Tap latency unchanged (page turn 110 ms, screen change 148 ms, small tap 5 ms), EPD self-test identical, upload over USB works.
- **Rule for code**: nothing to do per allocation; but a task that writes flash (the worker, the UI loop) keeps its stack in
  internal RAM (ESP-IDF: the cache is off during flash writes), and large fixed buffers should be heap blocks, not static arrays.
- **Found while measuring (fixed, 2026-11-16)**: an HTTPS download on the worker could reboot the board. Arduino's HTTPClient
  loops with `delay(0)` around `NetworkClient::readBytes()`, which returns 0 bytes at once while the TLS client has no data
  yet; `delay(0)` never lets the lower-priority IDLE0 run, so after 5 s the task watchdog fired (Woordle's word list fetch,
  backtrace through `writeToStreamDataBlock`). `platform/device/net.cpp` wraps the clients so a read that got nothing sleeps
  1 ms. Woordle's fetch then: 5616 words, internal lowest 70K. (The worker's priority cannot be 0: `esp_pthread_set_cfg`
  rejects it and would drop the whole config, core and stack included.)
- **Left**: Bluetooth is linked but never started (small gain, risk to the core build); Wi-Fi buffer counts; the reader's
  prefetch canvas costs 486 KB of PSRAM (of ~5.5 MB free).
- **Names in code**: the app is `wordgame`
  everywhere (directory, namespace, id, tests, docs); data saved under the old id is adopted once (apps/wordgame.md "Names").

## D28. The agenda comes from the phone's calendar, one way, through the encrypted relay

- **Source**: the calendars the phone's calendar app shows (`CalendarContract.Instances`, `READ_CALENDAR`), read by the
  companion app; not a Home Assistant calendar entity. Home Assistant only relays ciphertext, as for the inbox (D22): the
  calendar is as private as the messages, so the same trust rule applies.
- **One way**: the Paper shows, never changes. The phone sends `agenda` runs (COMPANION.md 7.1): today and the 13 days after
  it, a run replaces every day it covers, at most 12 events a day (the rest is a count), titles 80 and places 60 characters,
  no notes, attendees or descriptions. Declined and cancelled events stay on the phone.
- **When**: a changed day's run a minute after the calendar changed, the whole window once a day and after pairing or the
  switch; the Paper reads the relay as before (the Agenda app, the inbox, the scheduled refresh). Off by default on the phone
  ("Share the agenda"); switching it off (or taking the permission away) sends an empty run that empties the Paper.
- **Times** are the phone's wall clock; "now" and "today" on the Paper use its own clock and time zone (they normally agree).
- **On the Paper**: the Agenda app (Day: blocks on an hour grid; List: every event, paged), one line on the launcher and on
  the sleep screen with what is on now or next. The sleep screen's line is drawn at power-off (and by a scheduled refresh), so
  it can be hours old: it names the event, not "in 5 minutes".

## D29. Weather comes from Home Assistant's Google Weather entity, not from a weather service

- **Source**: the house already runs Home Assistant's Google Weather integration; its entity is `weather.thuis`
  (home-assistant `packages/google_neerslag.yaml`). The Paper reads that entity with the token it already has (D10): no Google
  API key on the card, no second host, no second TLS session, and the quota stays Home Assistant's. A weather service called
  straight from the Paper was not considered further.
- **Calls** (non-admin subset, D18; no `/api/template`): `GET /api/states/<entity>` for now, `POST
  /api/services/weather/get_forecasts?return_response` twice (`"type":"daily"`, `"type":"hourly"`) for the forecasts (the
  `forecast` attribute is gone since Home Assistant 2024.3), `GET /api/states/sun.sun` for the sun's next rise and set. Four
  requests per read over one kept-alive connection. Read only: the app never changes Home Assistant.
- **Entity id** is a default in code with an override in `/data/apps/weather.json` (`entity`), like the Home app's rooms: no
  Settings row until somebody needs one.
- **Places**: a place is a weather entity,
  because the Google Weather integration makes one entity per location; the Paper sends no coordinates and no address
  anywhere. Up to four (`entity`..`entity4`, `name`..`name4`), default Thuis (`weather.thuis`) and Office
  (`weather.office`); a `Segmented` switcher above the pages, a saved copy per place, a read only for the place shown.
  The location itself is added in Home Assistant's UI (it is a config entry, not YAML, so it is not in the config repo);
  Office was added there on 2026-11-16 (it costs the Google Weather quota of one more location).
- **What is saved** is the parsed model (`/data/cache/weather/<entity>.json`, about 4 KB), not the raw answers (the hourly answer
  is about 10 KB). The saved copy shows first; a "now" older than 90 minutes gives way to the forecast for the current hour.
- **Reads** happen when the app opens (if the last one is 15 minutes old) and every 15 minutes while it stays open, plus the
  refresh button. Nothing is read in the background or from the launcher: weather on the launcher or the sleep screen would
  need a scheduled wake (POWER.md) and is left for a later task.
- **Three pages, no scrolling** (`ui::Pager`): now with the next 16 hours and 4 days (5 without the place switcher); today and tomorrow as blocks of three
  hours (the "day block" view the owner asked for); all ten days. A block is clock-aligned (00, 03, ... local), so a column
  always means the same time of day; its icon is the condition most of its hours have, its temperature their mean, its rain
  chance the highest, its amount the sum (`dayBlocks`, unit tested).
- **Rain graph**: the next two hours from the house's own minute forecast sensor
  (`sensor.google_neerslag_minuten`, the house dashboard's rain graph data), as bars on the first page; a forecast older than
  40 minutes is not shown. Home only: the sensor is a REST call for the home zone, the office would need a second one in the
  Home Assistant config.
- **Not used**: UV, pressure, visibility, gusts, dew point, weather alerts.

## D30. Files over Wi-Fi: WebDAV and an upload page while one Settings page is open

Answers STATUS.md open question 5 with its option a, widened from "upload" to managing the books folder.

- **Where**: Settings > Storage > Files over Wi-Fi. The server runs only while that page is open and stops after 10 minutes
  without a request; leaving the page stops it. Wi-Fi and the device are kept awake meanwhile (`NetService::keepAlive`,
  `Os::keepAwake`).
- **What**: `/sd/books` and everything in it, nothing else (`..`, encoded or not, is refused; names starting with a dot stay
  hidden and cannot be written: the reader's `.paperos` cache, the uploads in progress). WebDAV class 1 and 2 (locks are
  pretended: Finder mounts read-only without them), so Finder (Go > Connect to Server) and Android file managers can list,
  upload, download, rename, make folders and delete; a plain page for any browser (list, upload with progress, delete).
  The Mac's `._name` and `.DS_Store` files are accepted and thrown away.
- **Protection**: a new six-digit code per start, shown on the panel, as the HTTP password (any name). Digest sign-in (macOS
  sends no Basic password over plain HTTP) and Basic (browsers); ten wrong codes lock the server for a minute. Plain HTTP on the
  home Wi-Fi: the files travel unencrypted, and the code is no secret from someone who can listen on that Wi-Fi (Basic
  sends it as it is; a captured Digest exchange gives it away to a try of all million codes, done in a moment). The code
  keeps other devices on the network out, it does not stand up to a listener: that would take TLS, which the firmware
  does not have for a server (D31). It lives only while the page is open. Port 80 on the
  device, 8080 in the simulator.
- **How**: an HTTP/1.1 server on BSD sockets shared by the device (lwIP) and the simulator (`src/platform/posix/http_server`),
  because macOS sends its uploads chunked and ESP-IDF's own server cannot read chunked bodies; the answers come from shared,
  unit-tested code (`core/file_server`). Uploads are written aside and renamed when complete.
- **Found on the device**: with Wi-Fi receiving, the card now and then refused a write (EIO), in 5 of 6 one-megabyte uploads;
  `fs::Writer` now reopens the file and writes the piece again at its place (up to three tries). Afterwards 9 of 9 arrived
  byte-identical, a few needing a retry. Speed: about 70-100 KB/s (the card's write speed).
- **Cost**: firmware +57 KB at the time (2.97 MB; 2.42 MB after the size pass of D31).
- **Later the same night**: the
  browser page gained New folder, Rename and Move (WebDAV MKCOL and MOVE from the page's own script), so a phone needs no
  WebDAV app; the reader's library finds books in folders and a moved book keeps its position (apps/reader.md).
- **Download** (2026-11-17): per file on the page, saved under its own name. Measured
  on the device: a download ran at ~30 KB/s with Wi-Fi power save on (Arduino's default, never changed before); with it off
  while serving (`hal::Net::setPowerSave`) 250-380 KB/s. It is switched on again when the server stops (a page destroyed
  without onHide leaves it off until Wi-Fi next restarts: Arduino keeps the setting).

## D31. Firmware size: no C++ exceptions, no iostreams, a trimmed ESP-IDF

The image went from 2,988,576 to 2,424,592 bytes (2.85 -> 2.31 MB, -564 KB, -19 %); the 3 MB budget has 721 KB left. The slot
itself is 5 MB (`app0`), so the budget, not the flash, was the limit. Nothing the user sees changed; tap latency is the same
(page turn 112 ms, screen change 152 ms, small tap 5 ms, `tools/latency.py` on the device).

| Step | Image after | Saved |
|---|---|---|
| `core/remote.cpp` parsed commands with `std::istringstream`: that one include linked libstdc++'s locale, wide-character and stream code (162 KB). A 60-line word reader replaces it. | 2,900,960 | 88 KB |
| ESP-IDF: no C++ exceptions (no unwind tables, no unwinder), the 44 common root certificates instead of 150, TLS client only, no SoftAP, no WPA Enterprise, no table of error names | 2,693,392 | 208 KB |
| Our code and the libraries compiled with `-fno-exceptions` (the Arduino build script still passed `-fexceptions`: every function carried clean-up code for unwinding) | 2,526,512 | 167 KB |
| `CORE_DEBUG_LEVEL` 3 -> 1: the Arduino core and its libraries log errors only | 2,506,144 | 20 KB |
| ESP-IDF: no mbedTLS error texts, silent assertions | 2,424,592 | 82 KB |

(The image grows in steps: its two flash segments are aligned to 64 KB, so a step's saving can show partly in the next one.)

What this costs, and the rules that follow:

- **No exceptions in firmware code.** `try`, `catch` and `throw` do not compile for the device. Nothing used them; a `throw`
  from the standard library (`std::stoi` on bad input, `at()` out of range, a failed allocation) ended in `abort` before and
  still does. Parse with `strtol`/`from_chars` and check sizes.
- **No `<sstream>`, `<iostream>`, `<fstream>`, `<regex>`, `<locale>` in shared code** (the simulator would accept them): each
  links 100-160 KB. `std::to_string`, `std::string`, containers and `<algorithm>` are fine.
- **TLS trusts 44 roots** (ESP-IDF's "common" list: Let's Encrypt, Google Trust Services, DigiCert, Amazon, Sectigo/Comodo,
  GoDaddy/Starfield, GlobalSign). A server under another root fails verification; to add one, go back to the full bundle
  (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`, +47 KB). Home Assistant over HTTPS read on the device; woordscore.nl chains to
  GTS Root R4 / GlobalSign Root CA, both in the list.
- **No access point mode.** ESP-IDF's SoftAP support is gone; Arduino's WiFi library still asks for the access point's network
  interface at start, so `platform/device/net.cpp` supplies `esp_netif_create_default_wifi_ap` returning none. A set-up
  hotspot would need `CONFIG_ESP_WIFI_SOFTAP_SUPPORT` back.
- **Crash output is shorter.** A failed assert prints its program address only; a TLS failure its number only. Decode an
  address with `xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/papers3/firmware.elf 0x42...` (`make monitor` does it by itself).
- **Tried and dropped**: `CONFIG_LWIP_IPV6=n` (ESP-IDF's lwIP port and the esp-modbus component do not compile without it).
  `-Os` for our code: it already was (the build script's `-Os` came after our `-O2`, which never had an effect).

Not done, and not wanted for now; do not ask again unless
the budget runs short (sizes from the linker map):

- **Fonts and icons, 473 KB** (11 text fonts at 4 bits per pixel, two icon sizes): the largest block left. On the card like the
  emoji atlases (the firmware then needs the card to show text), 2 bits per pixel (about half, coarser edges), or without
  Latin Extended-A (about a quarter less; ő, š, ł, œ become `?`).
- M5GFX and M5Unified carry the drivers and detection for every M5Stack board (about 50 KB a patch could remove).
- newlib's small printf (about 30 KB; loses `%llu` and friends, which the code uses).
- WPA3 (about 15 KB; WPA3-only networks would stop working).

