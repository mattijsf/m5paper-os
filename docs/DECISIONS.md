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
