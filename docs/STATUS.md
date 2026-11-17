# Status

Living file. Update it at the end of every task (see ROADMAP.md "How to work a task").

## Now

- **Phase 0 and Phase 1 are done** (2026-11-15). Shared code (`core`, `hal`, `ui`, `apps`) runs on
  the simulator and on the device: HAL, display manager (fast-then-settle, ghost budget), e-ink
  emulation with a refresh log, gestures, widget tree, fonts and icons (Inter, Literata, Lucide),
  main loop with timers and a worker thread, remote control over USB (`tools/device.py`), UI
  scripts with golden screenshots, 93 host unit tests.
- **P2-1 to P2-3 are done** (2026-11-15): `core/gesture`, `core/surface`, `ui/{widget,screen,layout,
  label,controls,list,tile,inputs,panels}`, `apps/uitest` (test screens `widgets1`, `widgets2`,
  `widgets2rows`, `widgets2list`, `widgets3`: `--app NAME` in the simulator, `>set screen NAME` on
  simulator and device). The widget kit is complete; next the app framework (P2-4), launcher
  (P2-5, P2-7) and settings (P2-6).
- **What the device runs**: the launcher with placeholder apps. Settings (asked for by the user on
  2026-11-15) arrives with P2-6.
- **P2-4 to P2-6 are done** (2026-11-15): app framework (`core/app`, `core/shell`, `core/store`,
  `ui/chrome`); the `hello` app compiles verbatim in the unit tests. The widget surface is `ui::Root`
  (renamed from `ui::Screen`; `pos::Screen` is the app screen). The device boots into the launcher
  (tiles Read, Home, WhatsApp, Mail, Settings; all show "Coming soon"); `tools/new-app.sh` creates apps.
  The Phase 1 demo screen is gone (Os without a Surface draws the font sheet or a blank screen).
- **P2-7**: keyboard, text field and TextBlock with pagination (`ui/keyboard`, `ui/textblock`), 14 unit tests for
  pagination (no overlong line, strictly increasing page starts, nothing lost, position survives re-layout,
  headings, long words, tiny pages, parser). **P2-6**: config service, secrets reader, time zone, Settings (all sections). The device shows the real
  Settings. **Phase 3 continues** (P3-1 device power HAL, P3-4 idle policy; Wi-Fi tasks wait for credentials).
- The factory firmware is saved in `backup/factory-16MB.bin` (restore command in HARDWARE.md).
- **P6-7 clipboard (D21), 2026-11-16.** `core/clipboard`; Paste / Copy all / Clear on a long press in every text field. The direct
  WhatsApp send of D21 is gone with the WhatsApp integration (D22): no Home Assistant script, no favourites (config key and the device
  test aid `>set favorite` removed); a reply goes to the phone through the relay (apps/inbox.md "Replying").
- **P6-12 clipboard sync with the phone (D23), 2026-11-16: built, simulator and JVM tests only.** `clip` both ways (COMPANION.md 7):
  every copy on the Paper is pushed to the phone (joining Wi-Fi; a push that failed offline is resent when Wi-Fi is next up); the phone sends on the share sheet ("Send to Paper"), the Quick Settings tile
  ("Clipboard to Paper") or the app's button; the Paper's clipboard sheet and the reply screen's Paste read the relay first. Switches
  on both sides (Paper: Settings > Home Assistant > Phone, config `phone.clipboard`). **Installed on the phone (unpaired), not run
  end to end** (the first real pairing exists since 2026-11-16, see below).
- **First real pairing, 2026-11-16 ~15:50**: the Paper and the user's Pixel 9 Pro (companion app) paired through the live relay; the
  phone's status file afterwards: relay service never restarted, 1 envelope accepted, 0 dropped, last push/read/ack 200. The phone
  sat on "Waiting for the Paper to confirm..." for a long time after both confirms; the cause is unknown (both logs had rolled over:
  the Paper keeps 50 lines, the phone's logcat started at 15:52). The phone reads `to_phone` every 3 s while pairing, so the delay
  was on the Paper's single `paired` push. Since then the Paper logs `paired to the phone: <result>, <seconds>` and resends an owed
  `paired` when Wi-Fi comes up (`core/paired_owed`, COMPANION.md 2 step 4). Known gap: if the first `paired` is lost and the Paper
  sends something else (a clip, a reply) before the resend lands, the phone's receive counter passes 1 and it drops the resent
  `paired`; it then needs a new pairing.

- **P7-9 the word game, 2026-11-16: built, simulator only.** A daily five-letter word game (docs/apps/wordgame.md). The owner asked for
  "woordle" from a handover text of that web game; built is PaperOS's own game in that genre (D26): own name, own word lists
  (688 answers, 2254 accepted words, `assets/wordgame/`), own first day and order, so **not the web game's word of the day** and a
  shared score had its own name (the user set it to "Woordle" later). Seventh app: the launcher now has a second page. The firmware builds with it; not flashed (Pending
  device checks). Open question 7.

- **P7-11 weather, 2026-11-16: done, live read checked on the device.** App `weather` (docs/apps/weather.md): Home Assistant's Google Weather
  entity `weather.thuis` for now, `weather.get_forecasts` (`?return_response`, daily and hourly) and `sun.sun`; three pages
  (D29): now with the next 16 hours and 4 days, today and tomorrow in blocks of three hours with sunrise and sunset, all ten
  days. **Rain graph**: the next two hours of Google's minute forecast (`sensor.google_neerslag_minuten`)
  on the first page for Thuis, bars with a headline ("Rain from 14:00"), one line when it stays dry. **Two places**: Thuis (`weather.thuis`) and Office (`weather.office`), a switcher above the pages, a
  saved copy per place (apps/weather.md "Places"). On the device both read (18:01: Office 7°, W 18 km/h, 10 days, 24 hours).
  The Office location was added in Home Assistant's Google Weather integration through its UI on 2026-11-16 (name "Office",
  entity `weather.office`); before that the Office tab showed the
  missing-entity screen (404). Saved copy first; not set up / token refused (401) / entity missing (404) / unreachable / stale (the hour's forecast
  replaces an old "now") shown. 13 Lucide weather icons.
  Goldens `weather`, `weather-office`, `weather-office-hours`, `weather-hours`, `weather-days`, `weather-offline`, `weather-stale`, `weather-stale-hours`, `weather-unset`,
  `weather-denied`, `weather-missing`; `fonts-sheet` (icon grid now 12 per row) and `wordgame-launcher-page2` (the new tile)
  re-accepted. **Merged into `main` on 2026-11-16** with the agenda and the emoji (icon lists joined, `src/gen` rebuilt; main's
  first rain graph, 466153b, gave way to the branch's). **On the device** (branch firmware flashed 2026-11-16 17:45, 2.85 MB, 54.3 %, without
  the agenda): log `[weather] read: partlycloudy, 10 days, 24 hours`; the live answers parse as the fixtures do (`templow`,
  `precipitation_probability`, `is_daytime` present, sunrise and sunset from `sun.sun`); frame dumps of the three pages show
  real values. Four requests over one TLS connection: 3.0 s (handshake) + 0.8 + 0.3 + 0.04 s after a 1.4 s Wi-Fi join.
  Memory (`tools/memory.py --only weather`): internal free 102K, lowest 69K, 102K after leaving (like Home, D27). Not
  checked on the device: the error screens (token refused, entity missing, unreachable: simulator only) and the panel itself
  (camera: gray of the block borders and range bars).

- **D22, 2026-11-16: the inbox moves to an end-to-end encrypted companion app; Home Assistant is not trusted with message text.**
  Done: both add-ons removed from Home Assistant (WhatsApp Gateway logged out first, then its integration, the add-on with its data and
  the FaserF repository; the Proton Mail Bridge local add-on with its data), the feeds emptied before that, the package cut down to
  `script.paperos_inbox_clear` + a recorder exclusion (home-assistant `ebc133d`), the orphaned entities removed. Live: both feed sensors
  answer 404 and the send script 400. The device shows "Not set up yet" for a missing feed (goldens `inbox-gone`, `inbox-gone-mail`) and a
  send ends in "Not sent: no send script in Home Assistant". **Next for the inbox: the owner's answers to open question 6**, then
  ROADMAP P6-8 to P6-11. The two leftovers on the Home Assistant disk (`/config/custom_components/whatsapp`,
  `/local_apps/protonmail_bridge`) were deleted on 2026-11-16 (contents checked first: integration code and
  add-on source only).

- **Companion relay and the Paper side, 2026-11-16 (P6-8 to P6-10)**: `docs/COMPANION.md` v1 (89aa3e3, handed to the Android
  session), the Home Assistant relay (home-assistant `e04d613`), libsodium crypto in `core/companion_crypto`, `core/companion`,
  Settings > Home Assistant > Phone (QR pairing), the inbox, scheduled refresh, replies and Somdoku's score on the relay. **Run on the
  device through live Home Assistant with a scripted test phone** (Python, pyca/cryptography; fake notifications only): the device's
  QR decoded, the same six-digit code on both sides, three encrypted notifications shown (`out/dev-relay/5-whatsapp.png`,
  `6-chat.png`, `7-mail.png`), the queue acked, the Paper's `paired` and `unpair` envelopes decrypted by the test phone; the device was
  unpaired and the queues emptied afterwards. Home Assistant restarted 2026-11-16 08:51 (`ha core check` passed first; after the
  restart: running, no repair issues, no failed config entries, no `whatsapp` entities, the relay sensors restored), so the recorder
  exclusion of the relay is loaded (not observed directly). The orphan `script.paperos_inbox_clear` entity (left by e04d613, no
  automation, script or dashboard referenced it) was removed from the entity registry. Next: the Android app (P6-C0 / P6-11, separate task),
  then a real pairing.

- **P7-10 Agenda, 2026-11-16: built; simulator, host unit tests (7 cases) and the Android JVM tests (`AgendaTest`, 5); not run on
  the phone or the device** (D28, apps/agenda.md, COMPANION.md 7.1 `agenda`). The phone's calendar, one way through the encrypted
  relay: the companion app's switch "Share the agenda" (off by default, `READ_CALENDAR`), the Agenda app (Day: time blocks on an
  hour grid with the now line; List), the next event under the launcher's date and on the sleep screen. Launcher page 2 now holds
  Woordle, Agenda and Weather. The APK is built (`android/app/build/outputs/apk/debug/app-debug.apk`), not installed.

- **Flags as letters, 2026-11-16 night**: the country code in an
  outlined box (`NL`), every letter pair plus ENG, SCO, WAL; Twemoji dropped. Atlases 2.5 MB, pushed in 19 s **while the
  device showed the emoji test screen**: reopened, it was identical to the new golden without a reboot (the reload of a
  replaced atlas, `fs::changes()`, now also checked on the device).
- **Emoji sequences and flags, 2026-11-16 night**: 224 ZWJ sequences and keycaps from Fluent,
  262 flags from Twemoji in gray with an edge, 1891 emoji in all (2.1 MB on the card, pushed in 16 s); families show 👪.
  On the device the `emoji` test screen is **identical** to the simulator. Found on the way: atlases pushed while the device
  ran were read through the index of the old files (blank emoji at body size) until a reboot; now the loader notices a
  replaced file (`fs::changes()`) and reloads (unit test; on the device the second push was skipped as identical, so that
  path ran on the host only).
- **Emoji on the device, 2026-11-16 evening**: main (merge a002da2 + the emoji test screen) flashed; the four atlases pushed
  to `/sd/paperos/emoji/` with `tools/push.py` (1.56 MB in 12.2 s, 110-131 KB/s, CRC checked). `set screen emoji` (test
  screen, golden `emoji`): the frame dump is **identical** to the simulator's. First open 518 ms of UI loop (about 70
  first-time card reads and the whole screen built; a text-only test screen's first open: 464 ms); later opens of both under
  the 400 ms the loop logs. The live view (`tools/view.py --lan`) was stopped for this.
- **Emoji on the panel, 2026-11-16**: Fluent Emoji High Contrast atlases
  (Noto Emoji for the rest) on the card,
  drawn by the fast text path (UI.md "Emoji"); `core/text_clean` keeps emoji. Simulator and host tests; not on the device yet.

- **Home lights like the dashboard's Thuis tab, 2026-11-16 night**: the sofa lamp first, the dining scene, the all-downstairs switch, lamp names in the sheets, the hall
  light gone; `home.json` version 2 replaces the device's old internal copy by itself. On the device against the live Home
  Assistant (read only, nothing switched): all 17 lights read, both pages show real states (`out/dev-home/`).

- **Firmware size pass (D31), 2026-11-16 night**: 2,988,576 ->
  2,424,592 bytes (-564 KB, -19 %), nothing visible changed. `<sstream>` out of `core/remote.cpp` (it linked 162 KB of locale
  code), no C++ exceptions (ESP-IDF and our build), 44 common root certificates instead of 150, TLS client only, no SoftAP, no
  WPA Enterprise, Arduino logs errors only, no mbedTLS error texts, silent assertions. New build rules: no `try`/`throw`,
  no iostreams in firmware code. **On the device** (flashed 22:37): boots, card and LittleFS mount, the launcher's frame dump
  is right (`out/size-device-launcher.png`), Home reads 17 entities over HTTPS with certificate verification
  (`out/size-device-home.png`), `tools/latency.py`: page turn 112 ms, screen change 152 ms, small tap 5 ms (as before). The
  flashed image also holds other uncommitted work of that moment (`reader/book.cpp`, `core/file_server.cpp`).
  One run of `make check` failed in `wifi_test.cpp:396` ("Wi-Fi goes off after the linger time"), code this pass did not
  touch; two runs before and two after passed (286 tests, 231 screenshots): not explained, watch for it. What is left to
  take: D31 "Not done" (fonts 473 KB first); the user stops here (2026-11-17: "I think it is fine like this").
- **Files over Wi-Fi, the page manages too (2026-11-17)**: New folder, Rename, Move and Delete on the browser page (Chrome,
  headless, at phone width, against the simulator: every action); the library finds books in folders. On the device: a test
  book uploaded into a folder showed in the library (page 2), the folder was deleted again. Found: the page's cleanup used the
  app context after the app was gone when a screen switch skipped onHide; it now keeps its own pointer to the server. One
  unexplained restart happened during the test (no reason in the log; not seen again in the steps that followed).
- **Files over Wi-Fi (P7-4, D30), 2026-11-16 night**: Settings > Storage > Files over Wi-Fi serves the books folder as WebDAV
  and an upload page while the page is open, behind a one-time code. **On the device** (192.168.1.69, port 80), tested from
  the Mac: OPTIONS, PROPFIND, PUT with a length and chunked, GET, MOVE, COPY, MKCOL, DELETE, Digest and Basic sign-in, a wrong code
  refused, `..` refused; nine 1 MB uploads byte-identical after the card-write retry (5 of 6 had failed before it: EIO from the
  card while Wi-Fi received); a WebDAV client library (webdav4) passes list, upload, folder, rename, copy, download, delete. All
  test files deleted again (only the six books are on the card). **Not tried**: Finder itself (macOS showed a sign-in dialog on
  the user's screen before it connected, which a script cannot click), an Android phone, the page in a browser (the app's
  browser pane may not open LAN addresses). In the simulator: `tools/device.py --sim --sim-data DIR` gives it a card with a
  Wi-Fi network.

## What works today

| Thing | Command | Result |
|---|---|---|
| Simulator build | `make sim` | OK (first build ~3 min, then seconds) |
| Headless screenshot | `make shot` | `out/shot.png`, 540x960, 16 grays |
| Simulator window | `make run` | mouse = touch, S screenshot, G ghost overlay, L refresh log |
| Firmware build | `make fw` | OK, 2,431,376 bytes = 2.32 MB (46.4% of the 5 MB slot; 715 KB under the 3 MB budget) after the code review, the camera target and the file memory of 2026-11-17 (2,424,592 after the size pass of D31; 2.85 MB before that) |
| Unit tests / UI goldens / gallery | `make test` / `make ui-test` / `make gallery` | 308 tests; 238 goldens (2026-11-17, after the code review, the Wi-Fi scan page, the camera target and the file memory); `out/gallery.html` |
| Flash | `make flash` | OK over `/dev/cu.usbmodem101`, ~18 s |
| Device frame dump | `make device-shot` | `out/device.png`, pixel-identical to the simulator's frame |
| Live view in a browser | `make view` (`ARGS=--sim`, `ARGS=--lan`) | `tools/view.py`: the device's frame on `http://127.0.0.1:8137/`, click = tap, hold = long press, drag = swipe; ~760 ms from click to new frame; holds the serial port (other device tools refuse to start meanwhile) |
| Camera on the panel | `make cam-shot` / `cam-video` / `cam-compare`, `tools/cam.py around tap X Y` | C922 webcam; captures through one Terminal window (camera permission); `cam.py lock` (uvc-util) at the start of a session; video 30 fps at 640x480 |
| Serial heartbeat | `make monitor` | `[beat]` and `[sd]` lines every 5 s (the refresh benchmark is gone since P1-1) |
| Open books for the simulator | `make books` | 5 EPUB + 1 TXT in `sim-data/sd/books/` (not in git) |

## Measurements and checks on the device (2026-11-15)

- Board detected by M5Unified as id 19 (M5PaperS3); display reports 540x960 without extra rotation.
- PSRAM 8388608 bytes; ~6.5 MB free after the 518 KB canvas and M5GFX's EPD buffers.
- Internal heap free after boot: ~282 KB.
- Battery: 3716 mV = 52% rising to 3930 mV = 77% while charging over USB.
- Refresh times (push + waveform): Quality full 1363 ms; Text full 1371 ms; Text row 540x96
  871 ms; Fast box 200x200 268 ms; Fast full 504 ms.
- Flash: 16 MB quad (manufacturer 0x46, device 0x4018).
- **Touch works and is aligned with the display**: the user tapped the Settings tile and the
  log showed `[touch] x=169 y=730` (inside that tile, which spans x 24-258, y 640-836); the
  Fast refresh of the dot took 258 ms.
- **SD card works**: mounts at `/sd`, 60917 MB, nearly empty.
- **`/sd/paperos_config` exists** with one key: `HA_TOKEN` (183 characters). Checked by key
  name and length only. No `HA_URL`, `WIFI_SSID` or `WIFI_PASSWORD` yet.

## How the owner wants to work (2026-11-15)

- Autonomous: no git questions (commit to `main`), no "please look at the device". Show
  progress with simulator screenshots sent to the user; verify the device through serial log,
  frame dump and injected taps (task P1-8, scheduled right after P1-2).
- Design previews (mockups of launcher, reader, Home, inbox) were shown in chat on 2026-11-15;
  no objections recorded yet. The source of truth for the look stays `UI.md`.

## Decisions taken with the owner (2026-11-15)

| Topic | Answer | Recorded in |
|---|---|---|
| WhatsApp | Not phone notifications: a Home Assistant plugin reads and stores messages, the device reads the result | DECISIONS.md D16, BACKEND.md |
| Mail | Proton Mail, through Home Assistant if possible | D17 |
| Home Assistant token | The owner put it on the SD card: `/sd/paperos_config`, `HA_TOKEN=` | D19, apps/settings.md |
| WhatsApp data | Lives in a hardened Home Assistant sensor (recorder-excluded, on no dashboard); the add-on keeps running despite its open endpoints | D20, BACKEND.md |
| WhatsApp group names | Stay "Group": the integration's Chats sensor stays disabled (it would expose the chat list to every Home Assistant login) | BACKEND.md |
| Home Assistant trust (2026-11-16) | Not trusted with message text ("too many integrations that might go rogue"): no plaintext in Home Assistant, send limits on the phone; companion Android app with end-to-end encryption; both add-ons removed | D22, BACKEND.md "Companion app" |
| Home Assistant URL | The public HTTPS address, set by the user as `HA_URL=` on the card; not hard-coded or committed; works away from home. TLS cost: one kept-alive connection per refresh | D18 (note), BACKEND.md "Connection" |
| Home Assistant token, during development | The token on the card stays as it is; after development it is replaced by a token of a dedicated non-admin user. Code stays within the non-admin API subset (per-entity `GET /api/states/<id>`, service calls; no `/api/template`, no event firing) so the swap needs no code change | D18 |
| Books | SD card is in, empty: find open books ourselves | `tools/fetch-books.sh`, task P4-0 pushes them to the card |

## Not verified yet (do not assume)

- **Reader on the physical panel (P4-6 done 2026-11-16, numbers in apps/reader.md "Device check" and DISPLAY.md "Reader ghosting")**: the measurable part (ghosting after 10 and 30 turns, Fast then settle, the cover's
  grays, the Clean flash, the turn time on the panel) moved to "To verify with the camera". Still for a person's eyes, once, batched: is S
  24 px comfortable to read, is the bold double strike at S/L/XL acceptable, how the menu sheet feels.
- **What the panel physically shows** (Fast then settle, ghosting, gray ramp, sharpness, the touch dot): see "To verify with the camera".
- Light-sleep current, touch wake from light sleep, power-off and RTC wake (task P3-1).
- Whether a side-button click is readable by a running program (task P3-1).
- Home Assistant: entity ids come from the dashboard repo, not from the live instance; nothing
  has been requested from the live instance yet (tasks P5-1, P5-2).
- Which Home Assistant user the token belongs to. The Home app and the inbox calls have **not been run with a non-admin token** (D18 assumes they work); the swap to a dedicated non-admin user comes after development.
- **Inbox since D22 (2026-11-16)**: the Android app does not exist yet; the relay and the Paper side ran only with a scripted test
  phone (above). Earlier note: The "Not set up yet" state ran in the
  simulator and on the device against the live 404 (Pending device checks). The WhatsApp linked device is gone: the user
  checked the phone's "Linked devices" list on 2026-11-16 and Home Assistant is no longer listed (the logout had run, Baileys
  logged "Intentional Logout"). Neither Home Assistant nor the Green has access to the WhatsApp or Proton accounts now.

## P6-1 progress (2026-11-15, done with the owner in Home Assistant; history: the add-on was removed on 2026-11-16, D22)

- Add-on repository `https://github.com/FaserF/hassio-addons` added; "WhatsApp Gateway" 2.1.10 (slug `605cee21_whatsapp`)
  installed, running, start on boot, auto-update off. Options: `admin_notifications_enabled` false, `admin_numbers` empty,
  `mark_online` false, `welcome_message_enabled` false, `sync_full_history` false, webhook off. The add-on wrote its integration
  2.1.10 to `custom_components/whatsapp` (`auto_install_integration`). Add-on facts: host network, port 8066, `/config`
  read-write, API key shown in its web UI (BACKEND.md).
- The user accepted the terms-of-service risk on 2026-11-15 ("Install the add-on it is fine").
- **Update 2026-11-15 19:51**: the WhatsApp link is up (QR scanned in the add-on's web UI; the session survived an add-on restart). Facts and warnings (number pairing rewrites 0 to 49, the setup dialog voids a shown QR, Baileys 7.0.0-rc14 pairing bugs, no `ui_auth` = every private-network address is superadmin and the API key goes out over mDNS) are in BACKEND.md. Options now also `log_level` info, `mask_sensitive_data` true.
- **Update 20:12-20:14**: the integration is set up (API token pasted by the user), connected, Home Assistant restarted, `sensor.paperos_whatsapp_inbox` restored, recorder exclusion verified (BACKEND.md "Integration set up"). **Still not verified**: a real incoming WhatsApp message in the feed (P6-1 goes to [x] when one is seen), the clear script with a non-admin token, the 48 h expiry, group names.
- **Update 20:24:31**: a real incoming WhatsApp message (direct chat) reached `sensor.paperos_whatsapp_inbox` 5 s after it was sent: one item with exactly the contract keys `chat`, `from`, `id`, `key`, `text`, `ts`; `from` is the contact's display name, `chat` is "", `key` ends in `@s.whatsapp.net`, `ts` is a number from `raw.messageTimestamp` (10 s old when read), `id` has the shape `<ts>-<number>-<6 chars>` (checked by shape and lengths only, not content). **P6-1 is done**; the WhatsApp half of P6-3 is verified for direct chats. Open: a real group message in the feed, the 48 h expiry, the mail half.
- **Older list, partly done**: restart Home Assistant to load the integration; UI password (`ui_auth_enabled`, the user types it); add the
  integration; the user scans the QR code; capture one real `whatsapp_message_received` event (direct, then group); then P6-3.
  Check (1) phone notifications still arrive, (2) events captured, (3) auto-responder, bot commands, mark-as-read off.
- The Home Assistant config repo is not touched by this note.

## Pitfalls found in Phase 4 (device)

- **The ESP32-S3 ROM has its own `tinfl_decompress`** and `esp32s3.rom.ld` binds that name to a ROM address, so a vendored
  miniz silently uses the ROM copy, whose decompressor struct is smaller: heap corruption on the first EPUB entry
  (`CORRUPT HEAP`, backtrace in `free`). `lib/miniz/miniz_defs.h` renames our tinfl functions to `pos_tinfl_*`. Check with
  `xtensa-esp32s3-elf-nm firmware.elf | grep tinfl_decompress` (ours is a `T` in flash, not an `A` at 0x4000xxxx).
- **The worker must not take the UI core.** Default pthread priority 5 on the loopTask's core froze the UI for 17 s while a
  book was indexed. The worker now runs at priority 1 pinned to core 0 (`platform/device/main.cpp`); a CPU-bound job on core 0
  at that priority starves the idle task and the task watchdog resets the board (mid-write to the SD card, which then
  answered nothing until the board lost power: `>poweroff N`). The indexing therefore sleeps 1 ms every 40 ms (`cooperate()`
  in `apps/reader/book.cpp`); any new long worker job must do the same.
- **SD card writes are slow: about 90 KB/s** (reads and inflate are fast). Write less (the reader converts EPUB chapters on demand
  instead of caching text; the cover is written after the book opens).
- **Internal heap is small** (about 200 KB, 12 KB left with a 40000-entry `unordered_map` width cache): big caches are fixed
  tables in one allocation (PSRAM), never many small nodes.
- **The device naps when no host has the port open** (`HWCDC::isPlugged()` is false then): a command sent meanwhile is lost,
  `tools/device.py` now sends it again. **Root cause of the unattended power-off**: auto-off was "never while a USB host is
  connected", but a Mac suspends the port when no program has it open, so the device saw no host and switched itself off after 15
  idle minutes. Fixed: USB (host or charger) seen this boot suppresses auto-off for 12 h (`kUsbGraceMs`, unit-tested in
  `power_policy_test.cpp`); firmware flashed only after the next time the device is on (Pending device checks).

## Done on the device 2026-11-15 (firmware 91f4a64)

- Inbox against the live feed (20:20-20:22, run in parallel): two fake events fired into Home Assistant appeared in the WhatsApp app (group row "Test Two - Group", a direct row, a chat page with accents and the euro sign correct; `out/dev-wa-list.png`, `out/dev-wa-chat.png`). Cold fetch: Wi-Fi join 1.49 s + HTTPS 1.30 s. "Mark all read" emptied `sensor.paperos_whatsapp_inbox` in Home Assistant, so the device token may call `script.paperos_inbox_clear` (whether it is non-admin is still unknown), and the app showed "No new messages" (`out/dev-wa-cleared.png`). **Still unverified**: a real message in the feed (a real group message arrived at 20:21:35 but the test's own clear removed it first). Found: the status bar showed Wi-Fi off 12 s after a fetch (linger is 20 s); "Mark all read" clears messages that arrived after the list was fetched.
- P5-2: all 30 entities of the default `home.json` (three tabs, 18 + 9 + 3 reads) exist on the live instance and answer 200 (the tab status
  line read "Updated", no "not found"); the Lights tab refresh took 1.4 s with Wi-Fi up (18 requests over one kept-alive TLS connection,
  36-52 ms each, `reused=1`), below the 3 s limit: no aggregate sensor needed yet. Read-only: no toggle or scene was sent.
- P7-6: the swipe from the status bar opens the quick panel with the real touch panel (injected swipe); Wi-Fi and battery detail show.
- **P7-2 UI scripts on the device (21:15-21:35, `tools/device.py script`, frame dumps in `out/device-ui/`)**: `textblock` 5/5, `widgets1` 1/1,
  `widgets2` 3/3, `widgets2list` 5/5, `widgets2rows` 3/3 shots **identical to the goldens**; `widgets3` 6/12 (the six others differ by
  421-1205 anti-aliased pixels: a shot between the Fast pass and the settle pass); `display-lab` 0/5 (2300-2950 pixels: its date line
  and counters are live); `launcher` cannot be compared on this device: the first shot differs (live summary line), and the Read tile
  reopens the stored book full-screen, so every later tap lands on a book page (the script moved nothing but page turns in Alice).
  **Found and fixed**: twice the device stopped answering for 50-60 s in the middle of a script, with the port open: the "USB host
  connected" reading dropped out, the idle policy light-slept for 49.7 s (`[power] light sleep asked 49757ms`) and the serial link was
  gone for that time. `decideSleep` now counts a host seen in the last 10 minutes as connected (`kUsbHostHoldMs`; it also keeps the device awake
  between two tool runs, which used to cost up to a minute per first command); after the fix the script ran past that point. The last run of `launcher.script` then hung in `tools/device.py` itself after the seventh shot (the device kept answering other
  commands; the serial write had no timeout). **The tool can no longer hang**: writes time out after 5 s, every resend is printed,
  and the whole run ends with exit code 3 after `--limit` seconds (default 90, a script: from its length).
- **P7-5 scheduled refresh (later the same day, RTC wakes only, on USB power)**: `poweroff 20` three times and `poweroff 20 30` once. Each
  RTC wake was recognised as a scheduled wake, skipped the first panel refresh (`[boot] the panel keeps its image`), fetched the feeds and
  logged a line in `/data/refresh.log`: refresh 2.8 / 5.2 / 2.9 / 3.0 s, 3.5 / 6.1 / 3.6 / 3.7 s since `setup()` (Wi-Fi join 1.2-1.4 s, TLS
  0.6-2.9 s). `poweroff 20 30`: the first wake refreshed and **powered off again by itself**, the second wake came 35 s later, refreshed
  and stayed on (launcher). One of the two feeds answered each time (WhatsApp; the mail sensor does not exist yet, P6-2) with 0 unread.
  Numbers and the mAh estimate (an estimate: no current meter): POWER.md "Scheduled refresh". Found on the first run and fixed: the boot
  pushed a blank first frame before the refresh (now `Surface::keepsPanelAtBoot`).

- Power-off and resume (2026-11-15, `>poweroff 20`, now calling `prepareForPowerOff()` like auto-off, RTC wake only): the board lost power 2 s after the command and the port came back 22 s after it
  (the 20 s alarm); the device answered 24 s after the command. The reader reopened Alice on the same page: the frame dump after the boot is
  **identical** to the one before the power-off (crc ebda3f52); `Reading: ...` shows in the launcher summary line on the device. The book opened
  1.6 s after the boot (cached index and the chapter converted from the EPUB).

- Status bar Wi-Fi icon (2026-11-15): the old Shell cleared the new Shell's net callback when a screen change replaced it (`set screen`, `restart`), so the icon stayed off while connected. Fixed (`NetService::onChangeOwner`); on the device the icon is on 14 s after a fetch (`out/dev-wa-14s-fixed.png`). That frame also shows a real WhatsApp message in the feed (a test message, 20:24), seen on the device at 20:27.

- P3-5 preparation on the device (2026-11-15): `tools/device.py cat /data/power.csv` returned the header and the boot line `1794771228,4160,idle,0,1,1,` (4160 mV, on USB). The battery sessions themselves are open.

- **First feedback from a person's eyes on the panel (2026-11-15)**: "The message line in the WhatsApp list is somewhat hard to read on a real device. The light gray
  text is a bit hard to see (the display itself is also a bit gray by itself)." Done: `secondary` gray(5) -> gray(3), `disabled` gray(9) -> gray(7), content lines in ink
  at the body size (UI.md "Colour", DISPLAY.md). **Confirmed by the user on the panel (2026-11-15): "Readable now"** (the WhatsApp list: message line in ink at the body size). Not yet looked at: gray(3) metadata on other screens, the gray(11) hairlines.

- Display lab on the device (2026-11-15, firmware 497b441): the four patterns were opened with injected taps and dumped (`out/dev-lab-ramp.png`, `-checker`, `-white`, `-text`);
  they equal the simulator goldens except the counters line (1812 to 2866 differing pixels, the push counts and ghost score differ). Checker and White went out as Fast
  passes. **No tuning claim**: how the ramp, the ghost after Checker then White and the gray text look on the panel needs a person's eyes.
- The WhatsApp list with the readable text is left on the panel (`out/dev-wa-readable.png`, `out/dev-wa-left-on-panel.png`); the feed was not cleared.

- Home Assistant side (2026-11-15 20:43, in parallel): `ha/paperos_inbox.yaml` installed (Home Assistant repo commit 80e19b0, byte-identical), `before` and the group
  lookup tested in Home Assistant's template engine and live (BACKEND.md). Group names stay "Group" by decision (the Chats sensor stays disabled).

- **User's eyes on the Display lab Text pattern (2026-11-15)**: "Caption above 3 becomes hard to read; Body above 5 is hard to read; Line 13 I can't see, 11 is VERY thin / subtly
  visible, too thin to be usable; S24: yes." Done: `divider` gray(11) -> gray(9), `disabled` gray(7) -> gray(5); `secondary` stays gray(3) (the caption limit). Limits in UI.md.
  Still to look at: the gray(9) dividers in a list, the gray(13) icon wells of the launcher tiles.

## Done on the device 2026-11-17 (firmware f2dda37 + the camera tool's new number; flashed 09:50, 2,428,912 bytes)

The review's refactor, the flash patch and the Wi-Fi page, with injected taps, frame dumps, the serial log and the camera
(the device had been put back under it: registered afresh, 149-248 SIFT inliers per image, exposure 25 ms, focus 40).

- **Serial path (`Remote::feed`)**: status, frame dumps (CRC ok), a 300 KB upload with `tools/push.py` (124 KB/s) and its
  delete; a command line of 614 bytes answers `#err line too long` and the next command works; `>rm` of `/sd/paperos/`,
  `/sd/paperos/.`, `/sd//paperos`, `/sd/paperos_config`, `/sd/paperos/../paperos_config` all refused, the folder's
  entries unchanged.
- **Driver**: `epd-selftest`: Fast and Quality identical. Flash patch: DISPLAY.md "Unchanged pixels are left alone" (the
  white background darkened 66-82 % of contrast in every settle and 25-31 % in every Fast pass; with the patch under
  0.5 %; grays after the settle unchanged; ghost after 20 navigations 0.75 % against 1.09 %).
- **Card and flash writes**: six page turns in a book (the reading position is rewritten on the card: FAT, where a file
  is replaced by stepping the old one aside), `>poweroff 20`: the board came back, the scheduled refresh joined Wi-Fi and
  read both feeds over HTTPS (3.8 s), the book reopened from its cached index on the same page (frame dump identical,
  crc e24e472d before and after); no `.old`, `.tmp` or `.part` file left in the reader's cache.
- **Files over Wi-Fi on the card** (curl from the Mac, 14 checks): upload, upload over the same name, chunked upload,
  COPY over a file, MOVE over a file (bytes compared by MD5 each time), a request with a 9000-byte header line closed
  without being run, delete; nothing left behind (`.upload-*`, `.copy-*`, `.old`).
- **Wi-Fi "Join a network"**: 9 networks after the first scan, 10 after the later ones (one the first scan had missed),
  two pages, rows stayed in place, the card's network marked "Saved". The reader menu, built with the new sheet helper,
  opens and "Library" leaves the book.
- Side effect of the test: "The Time Machine" was opened and turned to page 6 (the book the device reopened; the position
  of Alice is untouched).

## To verify with the camera (P7-7)

- **Fast pass = ordered dither: done 2026-11-16.** Confirmed on the panel by the camera session with Display lab "Fast" (firmware 8f221ab): 16 bands of steadily rising lightness (numbers in DISPLAY.md "Fast then settle"). The simulator's Fast emulation (70dba80) stays. "First the text, then the lines" was the old simulator emulation: on the panel a gray divider is in the first pass as a dotted line. Images (local): `out/cam-diff-fastramp.png`, `out/fast-ramp-zoom.png`.

The webcam (`tools/cam.py`, DISPLAY.md "Camera on the panel") checks these instead of a person's eyes. Numbers are per session (the
set-up moves). Webcam: C922, connected 2026-11-16; `cam.py lock` (uvc-util) fixes exposure, white balance and focus; 30 fps at 640x480 for video.

- **Ghosting, known page: done 2026-11-16**: Checker then White shows the checkerboard on the panel; the Clean removed 0.8-1.1 % of
  contrast in the checker area (0.3-0.6 % elsewhere); the Quality flash is visible for about 1.1 s (DISPLAY.md "First results").
- **Reader (P4-6): measured 2026-11-16** (DISPLAY.md "Reader ghosting"): ghost 1.23 / 1.54 / 1.99 % after 1 / 10 / 30 turns at M,
  1.36-1.71 % after 10 turns at S..XL; **no automatic clean in 30 turns** (fixed the same day: `Hint::Page`, a clean every 10th
  page, no settle sweep; ghost after 9 pages 1.21 %). Page turn on the panel: visible
  about 0.5 s after the tap (incl. ~0.2 s camera latency), settled about 1.6 s after it. Open: whether 2 % of contrast is visible to
  the eye (one look by the user, batched), the cover thumbnail's grays.
- **Fast then settle** in general (P1-3): `around tap` on a toggle and on a list row: the rect's visible span against the push times,
  the edge quality of the touch dot; the Clean flash (quick panel or Display lab) as a video.
- **Gray levels**: the Display lab Ramp: **measured 2026-11-16** (DISPLAY.md "First results": monotonic, g0..g6 crowd near ink,
  g12..g15 near white, g11 to g12 a big step); still to do: the gray(9) dividers in a real list, the gray(13) icon wells of the launcher tiles, gray(3) metadata on other screens. Whether
  the text is comfortable stays a question for a person's eyes.
- **Sleep screen** (P3-6, P7-5 item 2): a still after the device powers off (the cover's grays, the summary line), and stills an hour and
  several hours later: does it stay sharp.

## Somdoku and the send script (2026-11-16, night, autonomous)

(The send script below was removed the same morning with the add-on, D22.)

- **Send script installed** in Home Assistant (home-assistant commit `11371c7`, merged with the mail
  feed added separately: `ha/paperos_inbox.yaml` is byte-identical to the installed file). Guards tested live through
  the API with return_response: empty, too_long, a list target and an `@lid` JID were refused and nothing was logged as sent;
  **one real message to the user's own number** (approved) went out (`ok`, message id 22 chars), and a second
  call right after it was refused `too_soon`. Not tested: the hourly limit live, a group target, the device calling the script.
  The recorder exclusion of the new sensor and event needs the next Home Assistant restart (they hold only timestamps).
  Note: chats whose key is an `@lid` JID cannot be answered (the JID guard allows only `@s.whatsapp.net` / `@g.us`).
- **Somdoku (P7-8)**: see docs/apps/somdoku.md. 500 days of puzzles match the web game. On the device (firmware b5bcb97,
  2026-11-16 04:23): the launcher shows the Somdoku tile; opening it generated puzzle 6 on the worker and showed the ready
  screen (frame dump `out/dev-somdoku-ready.png`, same layout as the golden). Start was **not** pressed on purpose: it would
  start the user's clock on today's puzzle and the remote cannot reset `/data/apps/somdoku.json`. The log showed two
  `[perf] UI loop gap` of ~1.08 s around opening the app: check whether the generation (worker) or something else stalls the UI.
- **Somdoku review (2026-11-16, side task)**: vectors regenerated from the current somdoku repo (a41a212): byte-identical,
  500 days match. One divergence fixed: letting the selected cell go (tap it again, tap beside the grid) or pausing ended the
  slip window, so wiping and refilling the last cell then counted as a change (yellow) where the web game keeps it green.
  "Stuur naar WhatsApp" removed;
  the done screen keeps "Kopieer score" only, the score is pasted into an inbox reply. `whatsapp::sendToFavorite` stays in the
  inbox API, now without a caller.
- **Clock lost (2026-11-16)**: Somdoku and the word game show "Klok zetten" when the clock was never set (`ctx.syncClock`: one Wi-Fi
  join, wait for the SNTP sync). The simulator's `Clock::set` now ends `--no-rtc`, so the flow runs headless
  (`somdoku-clock`, `wordgame-clock`). Pending on the device: a real flat-battery start (clock lost) and the button.
- **Flaky unit test**: `wifi_test.cpp:396` ("Wi-Fi goes off after the linger time") failed once in four runs on 2026-11-16
  (`msToNextDeadline()` 19999 instead of none: the linger restarted 1 ms late). Not investigated yet.

## Pending device checks
- **After the device run of 2026-11-17** (below, "Done on the device 2026-11-17"), still open: the flash patch for the
  user's eyes in daily use (the camera says the sweep is gone; whether anything else looks off, e.g. patches of white, is
  for a look); Finder and a phone on Files over Wi-Fi (curl only so far); the Wi-Fi scan page with a real finger while
  the list grows.
- **Size pass (D31)**: with the trimmed firmware, not run yet: the word list fetch from woordscore.nl (its roots are in the
  44-certificate list, but the fetch runs at most once a day), Files over Wi-Fi (the server's sockets are unchanged; TLS server
  and SoftAP code are gone), a Wi-Fi reconnect after sleep, a night of normal use (silent assertions: a crash shows an address
  only).
- **Files over Wi-Fi (D30)**: Finder > Go > Connect to Server with the address and the code (copy a book in, rename, delete); an
  Android file manager (WebDAV) and Chrome on the phone (the upload page); a long upload (20 MB) for the retry rate.
- **Emoji, rest of the device check**: with the camera (the C922 was not plugged in on 2026-11-16 evening), whether the Fluent
  High Contrast faces stay readable at 18 and 22 px on the panel (`tools/device.py set screen emoji`); a real WhatsApp
  message with emoji (the inbox was empty at the check).
- **P7-10 Agenda (D28)**: install the APK on the phone, switch on "Share the agenda" and grant calendar access (the user, once);
  check `files/status.json` `agenda_sent` > 0 and the Paper's Agenda app against the phone's calendar app (titles shown only on the
  Paper); an edit and a delete on the phone reach the Paper after a relay read; switching off empties the Paper. On the device:
  the day view's gray(12) blocks and ink bars with the camera (gray(12) is near white on the panel: the bar must carry them),
  the sleep screen line, `tools/memory.py --only agenda`.
- **Word game renamed `vijfje` -> `wordgame` (2026-11-16, simulator only)**: the device still runs the firmware with the old id.
  After the next flash: open the game once, then the statistics screen must still say 1 played and 5616 fetched words
  (`adoptOldData`), and today's finished game must still be there. `tools/memory.py --only wordgame`.
- **`paired` resend (2026-11-16, unit tests only, not flashed)**: after the next flash, on the next pairing the log shows
  `paired to the phone: pushed, N s` and `/data/companion_paired_owed` is gone; with the relay unreachable at Confirm (Wi-Fi off
  in the router or a wrong `HA_URL` for a moment) the phone activates once Wi-Fi is back (`paired to the phone, again: pushed`).
- **Toast as a band over the status bar (2026-11-16, simulator only)**: on the device one Fast push of 540 x 56 and the status
  bar back after 3 s (frame dump); with the camera whether the band leaves a ghost in the status bar.
- **The word game (P7-9), device run 2026-11-16 14:00** (firmware of main bb517a4, then the statistics line fix; injected taps, frame
  dumps in `out/dev-wordgame/`): the game screen is **identical to the simulator golden** below the title bar (`1-open.png`);
  a typed letter is a Fast push of its tile (70 x 70, push call 2-30 ms) and a settle 0.6 s later; "Onbekend woord" and "Te
  weinig letters" toasts, the row is not used; **a backspace is one Fast push, no settle** (five in a row, refresh log), the
  board afterwards identical to the opening frame; Statistieken; "Woordenlijst bijwerken": Wi-Fi join 1.36 s, HTTPS 2.87 s
  (TLS 1.40 s), `[wordgame] word list: 5616 words`, heap 101 KB during TLS, 144 KB again after leaving, Wi-Fi off. **Found and
  fixed**: the statistics line was cut off with the real numbers ("... opgehaald op 16-11-2..."): now day and month only, two
  lines allowed; flashed and seen on the device (`9-stats-fixed.png`). One camera still (`cam-typed.png`, dark: exposure not
  set for this session): board, typed tiles, keys and legend are on the panel; the gray(9) box of "zit er niet in" in the
  legend is hard to see in it. `[perf] UI loop gap ~1.07 s` lines come with each frame dump (two dumps, two lines).
  **Played on the device 2026-11-16 14:07-14:10** (today's puzzle, three guesses chosen from the
  answer to get every mark): guess 1 marked other / absent as computed, the row and the keys as two pushes (390 x 72, then
  the box of the changed keys 210 x 232; each Fast + settle); left to the launcher and reopened: **the frame is identical**
  (the saved game); guess 2 was a word that is **only in the fetched list** and was accepted (the cached list is loaded on
  the device), with a double letter marked once; guess 3 solved it: "Geraden in 3 beurten", "Reeks: 1 dag", the result
  screen as one Fast push + settle; "Kopieer score" toast "Score gekopieerd" (the clipboard file was not read back:
  `>cat` does not reach `/data`); Statistieken 1 / 100 / 1 / 1 with the solid bar at 3. Dumps `out/dev-wordgame/11` to `16`.
  **Camera** (`cam-guess1.png`, crop `cam-guess1-keys.png`, `cam-won.png`; exposure locked, the room is dim): hatched tiles
  and keys read as a gray ring around the letter, clearly apart from solid and plain; absent tiles: letter readable, frame
  faint; **used-up keys (gray(10)): letter faint but readable, outline almost gone**. Open: the lost screen on the device
  (simulator only), a real finger on the 45 px keys, whether the used-up keys are too faint for a person's eyes.
- **The word game (P7-9), older note**: (the firmware builds, see below): on 2026-11-16 the device and the
  `papers3` build were in use by other work (sdkconfig experiments; its builds exclude `apps/wordgame/`), the owner said
  "test in simulator". Done instead: simulator, unit tests, and `wordgame.cpp` + `words_data.cpp` compiled alone with the
  ESP32-S3 compiler (no warnings, 15 KB). To do: `make fw` with the app (size), flash, open it from launcher page 2, type a
  guess with injected taps (key positions from `debugState`), frame dump against the goldens; with the camera: how the
  hatched ring, the gray(5) letters of absent tiles and the gray(10) letters and outlines of used-up keys show on the panel
  (the panel prints mid-grays lighter than the simulator: a used-up key may nearly vanish there), and typing speed (a Fast push per
  letter plus its settle). For a person's fingers, batched: whether 45 px wide keys are comfortable.
  **Device compile, found by the tap-latency pass 2026-11-16**: `make fw` failed on `wordgame_app.cpp`
  (`Store::get("day", INT32_MIN)`: `int32_t` is `long` on the ESP32 and `Store` has no overload for it; the simulator
  build does not see this). Fixed with `wordgame::kNoDay` (an `int`); the three call shapes were checked alone with the
  ESP32-S3 compiler. **Firmware build with the app: done 2026-11-16 12:00** by the tap-latency pass (`make fw` at
  f859131 with its uncommitted core settings in `platformio.ini`): compiles and links; checked here: `firmware.elf` holds
  the `pos::wordgame` symbols, `firmware.bin` is 2,897,584 bytes (2.76 MB, 55.3% of the 5 MB slot; the 3 MB budget has
  248 KB left). Not flashed (a PSRAM soak was running on the device).
  **Memory on the device (memory pass, 2026-11-16)**: first reading, old configuration: 18 KB internal heap free after
  opening the app (108 KB idle), still 19 KB back in the launcher. With D27 (45d5d23: ordinary allocations and mbedTLS in
  PSRAM) and no fetch in that run: **141 KB free while the app is open (the idle level), 144 KB 30 s after leaving, PSRAM back
  to idle after closing**: the app keeps nothing, its 28 KB word list is freed with the screen. The low first reading was most
  likely the first list fetch (Wi-Fi + a new TLS connection) under the old configuration: fits, **not measured**; Home's TLS
  fetch now bottoms out at 69 KB (was 18 KB). To measure the fetch: Statistieken > "Woordenlijst bijwerken" (apps/wordgame.md).
  **The word list fetch on the device: done 2026-11-16** (memory pass, "Woordenlijst bijwerken", D27 firmware). The first
  tries **rebooted the board**: task watchdog on IDLE0, because Arduino's HTTPClient body loop spins around a TLS read that
  returns 0 at once, so the worker on core 0 never let the idle task run for 5 s. Not in the app: any HTTPS download on the
  worker could hit it; fixed in `platform/device/net.cpp` (54ad248: the clients sleep 1 ms on an empty read). After the fix:
  `[wordgame] word list: 5616 words`; dns 28 ms, TLS + TCP 1493 ms, request + headers 1402 ms, body 98 ms; internal heap lowest
  70 KB during the fetch (largest block 63 KB), PSRAM lowest 5.64 MB; Wi-Fi off 20 s later. Still open: flash + play-through
  with injected taps, the camera on the hatched tiles and the gray(10) keys, the fetch on the app's first open (the same code).
- **Screen passes with gray areas as one Text push** (`why=screen-gray`, 2026-11-16, simulator only: the device was reserved for other
  testing). With the camera, `around tap 36 92` (back from an app to the launcher), before and after the change: number of waveforms
  on the panel (expected one instead of Fast + settle sweep), the time until the final look and until the first visible change, whether
  the gray(13) wells show in that one pass, and whether a Text push over an area that held a Fast image leaves ghost. Same for opening the
  keyboard (a text field) and the Home app's tiles. If the Text push feels slow, `grayAreaMin = 0` in `DisplayParams` restores Fast then settle.
- **P6-12 clipboard sync** (D23): the APK of e18ee72 is installed on the phone (2026-11-16 09:30, `adb install -r` over wireless
  adb; the app is not paired and has no token, stage NONE). Checked: the share target and the Quick Settings tile are registered, a
  share while unpaired does not crash and queues nothing. With a real pairing still to do: copy on the Paper and check the phone's clipboard within a minute (the phone reads `to_phone` every minute); share
  text on the phone (share sheet and the tile) and long-press a field on the Paper: the sheet should say "Clipboard from your phone".
  Unknown until then: whether `setPrimaryClip` from the foreground service works on Android 17 (allowed for writes as far as known)
  and whether Android 13+ shows its own "copied" overlay for it.
- **Somdoku** (P7-8): flash, open, play and solve today's puzzle on the device; puzzle generation time on the worker; the
  board and the hatched squares on the physical panel (eyes, once); the clock across a light sleep and a power-off.

- **P6-7 (2026-11-16, firmware of this commit, flashed)**: run on the device: the text-field sheet on `widgets6` (frame dump identical to the
  golden `textfield-menu-empty`), Clear on the three-line field, and the WhatsApp compose screen with a fake favourite added in memory
  (`>set favorite`, removed since with the favourites): compose, typing "hi" with injected key
  taps, the confirmation sheet, Cancel (frame back to the compose screen, no send in the log). Dumps: `out/dev-textfield-menu.png`,
  `out/dev-compose.png`, `out/dev-compose-typed.png`, `out/dev-confirm.png`, `out/dev-note-menu.png`, `out/dev-note-cleared.png`.
  **Not run**: Send in the sheet (never, until the script is installed and the user chooses a chat), Paste with a real clipboard on the
  device, the Reply button on a real conversation (it would show the user's chats in the dumps). One injected long press sent right after
  the tap that closed a sheet (no wait) did not open the sheet and the following tap typed a space; with the sheet closed first it works.
  A real finger is untested.
- **P7-2**: the hang of `launcher.script` on the host was not reproduced after the fix (write timeout + run limit), only bounded; `launcher` and `display-lab` need a device-side way to be comparable
  (fixed clock and no stored book) or stay "data".

- **P7-5**: (1) the real path: auto-off or "Sleep now" on battery with the setting at 1 h, the device waking by itself an hour later and
  switching off again (the device tests used `>poweroff N [M]`, which calls the same `Os::powerOffForSleep`, with 20-30 s alarms; the
  hour-long alarm and the Settings value on the device were not run); (2) the sleep screen with the summary line on the panel: camera
  list above; (3) a count above 0 (the live feed had
  nothing newer than the seen marker) and the mail feed; (4) the current while awake (the mAh figures are estimates); (5) a side-button
  press that lands within -90 s .. +180 s of the alarm runs the refresh first (a touch then ends it in a normal boot: simulator only).

- **Inbox "Not set up yet"** (D22): **done on the device 2026-11-16 07:21** (firmware 49ed875): WhatsApp and Mail against the live
  Home Assistant logged `GET /api/states/sensor.paperos_*_inbox -> 404` and showed "Not set up yet"; frame dumps
  (`out/dev-d22/whatsapp.png`, `mail.png`) identical to the goldens below the status bar; launcher without badges. Not run: Somdoku
  "Stuur naar WhatsApp" up to Send on the device (simulator golden `whatsapp-send-missing` covers the 400 toast; nothing can be sent,
  the integration is gone).
- ~~Mark all read with `before`~~ and the real group message: dropped with the add-on route (D22).
- **Companion (P6-10)**: a reply typed on the device and pushed to `to_phone` (the push code is the one that carried `paired` and
  `unpair`, which ran); "Mark all read" sending `dismiss`; pairing with the real phone app; the scheduled refresh against the relay.

- **P7-6**: "Sleep now" ends in the sleep screen and a power-off (not run: only the side button restarts it). The swipe from the status bar works on the device (checked 2026-11-15).
- **P6-4/P6-5** (written before D22; the feeds are gone now): nothing of the inbox has run on the device or against real feeds. Unverified
  assumptions: the exact `imap.fetch` response shape (`service_response.text`), the optional `connected` attribute for the link-down
  notice, that a non-admin token may call `script.paperos_inbox_clear`, and the fetch with a real entry id and uid.
- **P5-5**: one run with a non-admin token before Phase 5 counts as done; toggle timing on the device (tap to visible change under
  0.4 s; not run, a toggle changes the user's real lights); app open with Wi-Fi off to fresh data (under 4 s: warm Lights tab measured
  1.4 s, cold adds the Wi-Fi join and TLS, about 2 s: not measured); whether polling needs the Wi-Fi linger extended.
- **P5-1**: Settings > Home Assistant "Test connection" now goes through `HaClient` (`core/ha`); re-run it on the device (it worked with the previous direct request, P3-7).
- **P3-6**: how the sleep screen looks on the panel: camera list above; button press to usable screen (an RTC wake measured instead, see below; a real side-button press not timed).
- **Auto-off guard** (kUsbGraceMs): built, not run on the device; check that an idle device on USB with no program holding the port stays on for more than 20 minutes.
- **Phase 4 (reader), device state (2026-11-15, side button pressed, firmware of 91f4a64; items below marked "later" ran with the next firmware)**: Pride and Prejudice cold open 12.1 s (1577 pages;
  convert 6.6 s, paginate 5.4 s), cached open 0.22 s; Alice (EPUB 3, images) cold 6.2 s, cached 0.23 s, text size L re-index 5.0 s (432 pages);
  page-turn render 95-157 ms, Fast push call 86 ms; frame dumps of page 1, page 4 and the cached page 4 of both books **identical to the
  simulator**; menu, contents (the books' real contents) and the text size change work with injected taps. **Later the same day**: the TXT book (Time Machine) cold ~1.5 s (write 0.3 s, layout 0.95 s), cached 0.22 s; the cover on the library card (first run showed the placeholder: the decode canvases were not in PSRAM; fixed in `ui/image.cpp`, then the real cover shows, `out/dev-library-cover2.png`). **Not run**: "Sleep now" from the quick panel (it needs the side button afterwards).


- P3-4: **auto-off on battery** (the policy never powers off while USB is connected, so the automation could not run
  it; covered by unit tests only) and that the UI still feels immediate when the device sleeps between taps
  (needs real touches); `UI feels immediate` after a touch wake.
- P3-1: **real touch wakes light sleep** and the waking tap is delivered (an injected tap cannot wake the chip);
  **side button powers on** after `powerOff(0)`; `powerOff(0)` itself (only the button brings the device back, so it
  was not run by the automation). Batch with the next time the user is at the device.
- P1-2: **real finger tap** logging `[touch] x= y=` through `Input::update()` (no tool can
  press the GT911). Injected taps (P1-8) cover everything after the input queue, not the
  translation from `M5.Touch`. Batch with the next time the user looks at the device.
- P1-2: boot without an SD card must log `no SD card` and continue (code path written, not
  run: the card stays in the device).
- P1-2: `Clock::set()` and RTC reads of a *set* RTC are not run on the device (the RTC is
  unset, `now()` = 0; setting it would write a made-up time). The date math was checked on the
  host against `date`. Do it when time sync exists (P3-2).

## Open questions for the user

1. ~~Where may WhatsApp data live? / add-on risk~~ (superseded by D22 on 2026-11-16: the add-on is removed) **Closed 2026-11-15**: a hardened Home Assistant sensor (DECISIONS.md D20); the terms-of-service risk was
   accepted at install; the add-on's unauthenticated endpoints (BACKEND.md "Add-on security") are an accepted risk, "Keep running". **Watch**: check each add-on
   update for auth on `/api/autoresponder/config` and `/api/dashboard`; the add-on's API token never goes on the SD card.
2. **Wi-Fi**: add `WIFI_SSID=` and `WIFI_PASSWORD=` to `paperos_config` on the card? It lets
   the device go online from task P3-2, long before the on-screen keyboard exists.
3. ~~**Proton**: paid plan (needed for Bridge)? OK to run Bridge as a Home Assistant add-on?~~ **Closed 2026-11-16**: Bridge is removed
   (D22); mail comes from Proton Mail notifications on the phone.

4. **P7-3 OTA update: how should it be protected?** Not built; pick one (or "no OTA").
   - **a. Pull only, signed** (recommended if OTA is wanted): Settings > "Check for update" downloads an image over HTTPS from one fixed
     address (a key on the card), checks an Ed25519 signature against a public key compiled into the firmware, then switches the boot
     slot; the new image must confirm a good boot or the old one comes back. No open port on the device. Needs a signing key on the Mac
     (never in the repo) and a place to host the file.
   - **b. Push on the home Wi-Fi, in an update mode**: the device listens only while a Settings screen is open (5 minutes at most) and
     wants a one-time 6-digit code shown on the panel; the image is still signature-checked. Nothing to host, but a port is open for
     that window and the upload is plain HTTP on the Wi-Fi.
   - **c. No OTA**: keep `make flash` over USB (works today, 18 s). Nothing to protect; the device has to be at the Mac.
5. ~~**P7-4 Book upload over Wi-Fi: how should it be protected?**~~ **Answered 2026-11-16: "Yes wifi file mode"** (option a,
   with WebDAV for Finder and Android file managers): D30, built.

6. **Companion app (D22, asked 2026-11-16)**: the answers decide P6-8 to P6-11; nothing is built before them.
   - a. **WhatsApp reply scope**: is replying only to chats that still have a notification on the phone enough? (No new message to a
     chosen chat, so Somdoku's "Stuur naar WhatsApp" would become "Kopieer score" plus a reply, or go.) **Answered 2026-11-16: it
     goes; Somdoku has "Kopieer score" only.**
   - b. **Mail**: are sender and subject (maybe a short preview) enough, without the full mail text?
   - c. ~~**Phone**: which phone model and Android version?~~ **Read over adb 2026-11-16**: Google Pixel 9 Pro (`caiman`), Android 17
     (SDK 37, security patch 2026-10-05), WhatsApp 2.26.45.13; adb over Wi-Fi at 192.168.1.51 (the port changes). Still to confirm:
     that this phone is the one that runs the app.
   - d. **Always on**: is that phone always on and online at home (the inbox only fills while it is)?
   **Assumed, not answered** (the owner said "Continue", 2026-11-16): (a) WhatsApp replies only
   to chats that still have a notification on the phone, through its reply action; no new chats, no favourites; Somdoku's "Stuur naar
   WhatsApp" works only while that group has a live notification, else a toast says so. (b) Mail: sender, subject and whatever preview
   the notification shows; read-only. (d) The phone is mostly on and online at home; both sides queue and survive the phone being
   away for hours. Protocol: docs/COMPANION.md.

7. **The word game (P7-9, D26)**: (a) ~~the word list is small: download OpenTaal?~~ **Closed 2026-11-16**: "Not now"; instead the
   app fetches the accepted guesses of woordscore.nl weekly (apps/wordgame.md "Fetched words"). (b) the word game is alone on launcher page 2: fine, or should
   the two games share a place (a 2 x 4 grid, a Games folder)? (c) the handover's `woordscore.nl/woordlijst.txt` answers
   10 + 30 words at first; **since 2026-11-16 about 11:50 the full list (844 + 5616)**. The app fetches its second part
   (accepted guesses) weekly; its first part (the web game's daily answers in order) stays out (D26). **Still the user's
   wish as far as understood: the web game's word of the day; that part is not built.**

8. ~~**Weather, Office (P7-11, asked 2026-11-16)**: Home Assistant has no weather entity for the office yet.~~ **Closed
   2026-11-16**: the location "Office" was added in the Google Weather integration (the owner pointed to the signed-in Home
   Assistant in the browser pane); `weather.office` reads on the device. The pin is approximate: move it
   in Home Assistant if it matters.

None of these block Phase 1, Phase 2, or the reader.

## Notes for later tasks

- **Flaky unit test seen once (2026-11-16, weather work)**: `wifi_test.cpp:396` (the linger switches Wi-Fi off after
  `lingerSec`) failed once in `make test` and passed in three reruns of the same binary. Not investigated.
- **Weather on the launcher or the sleep screen** (P7-11, 2026-11-16): the weather app wants neither yet. A line like
  "6° Partly cloudy, rain 70 % at 17:00" can come from the saved copy (`/data/cache/weather/weather.thuis.json`, `weather_model`
  `fromJson` + `nextHours`) without a read; keeping it fresh needs a scheduled wake (POWER.md). Left out because the agenda
  work was changing the launcher's summary line and the sleep screen at the same time.

- **`Store::get` / `set` with fixed-width integers**: pass `int`, `bool`, `double` or a string. `int32_t`, `INT32_MIN`,
  `int64_t`, `size_t` compile in the simulator and are ambiguous on the ESP32 (`int32_t` = `long`). Cast first.

- **Two builds in one checkout (2026-11-16)**: an edit of `platformio.ini` makes PlatformIO wipe all of `.pio/build`, so
  other builds' simulator and test programs vanish mid-task. Build into a private folder instead:
  `PLATFORMIO_BUILD_DIR=<dir> pio run -e sim` (and `pio test -e test --without-testing`), then copy `<dir>/sim/program` to
  `.pio/build/sim/program` before `tools/ui-test.sh`. Do not build `papers3` while another build rebuilds the core (D25).

- **Reader page turns (done 2026-11-16)**: `Hint::Page`: Fast without the settle sweep, a Quality clean every 10th page
  (`display.pageCleanEvery`). Measured on the device (DISPLAY.md "Ghost budget"). Open for a person's eyes, batched: whether the
  black/white text between cleans reads well at S and in italics (the camera cannot judge reading comfort).
- **Serial port**: never hold a `tools/device.py` Link open and unread while doing something else (camera shots): on 2026-11-16 a
  `>shot` went unanswered for 60 s after the port sat unread for ~15 s. `request()` now resends only until the device has answered
  once on a Link, then fails after one 10 s try.

- **UI test flake, fixed (2026-11-15)**: `inbox-error.png` differed from its golden in two of seven `make ui-test` runs (342 pixels: the
  footer line caught before its settle pass). The worker runs in real time while the script's clock is fake, so its answer lands at a
  slightly different simulated time; the script now waits 2500 ms before that shot (three runs in a row match). Same cure for any other
  shot that flakes: a longer `wait`, never a new golden.

- LovyanGFX picks the colour format from the C++ type of the colour argument. The spike first
  passed `uint8_t` grays and got RGB332 colours (wrong gray ramp). `pos::Color` is `uint32_t`
  for this reason. Keep it.
- `partitions.csv`: nvs must end before 0xE000 (otadata) and app0 must start at 0x10000.
- `pio pkg exec -p tool-esptoolpy -- esptool` fails ("Permission denied"); call
  `~/.platformio/penv/bin/esptool` directly.
- Device scripting: use `tools/device.py` (`log`, `shot`, `tap`, `wait-boot`) with
  `~/.platformio/penv/bin/python`. **Opening the serial port must not touch DTR/RTS**: setting
  them (`ser.dtr = False`) reboots the board on every open; plain `serial.Serial(port)` does not.
- Reading the device log from a script (older way): open `/dev/cu.usbmodem101` at 115200 with pyserial
  (`~/.platformio/penv/bin/python`) and read for ~10 s; the spike prints a heartbeat every 5 s.
  `pio device monitor` is interactive and does not suit scripts.
- The project has its own git repository (branch `main`, created 2026-11-15). Its parent
  folder is also a git repository (no commits): always run git from inside
  `m5paper-os` and check `git rev-parse --show-toplevel` if in doubt.
- Project Gutenberg's "EPUB3 with images" of Pride and Prejudice is 24 MB (illustrated
  edition): the fetch script uses the no-images build. A real-world reminder that the reader
  must skip images without loading them.
- gutendex.com (Gutenberg search API) timed out; the fetch script uses fixed book ids on
  gutenberg.org instead.

- P1-1 leftovers for later tasks: device `Clock::now()` returns 0 until P1-2 reads the RTC; SD
  and the `paperos_config` key report still use the Arduino `SD` library in `device/main.cpp`
  (P1-2 replaces it with the VFS mount, then `/sd/paperos_config` is read through `pos::fs`).
  `pos::fs::writeFile` retries as remove + rename if rename over an existing file fails (FAT):
  not exercised on the device yet. The simulator reads `sim-data/` relative to the working
  directory. `src/ui` and `src/apps` do not exist yet, so the P1-1 grep only covers core and hal.
  `hal/display.h` includes `core/gfx.h` for `Canvas`/`Rect`/`Refresh`.

- P1-2: device boot logs `[boot] clock now=`, `[storage] ...`, `[fs] /data/test.txt ...`,
  `[fs] boot count N` (`/data/boot_count`, rewritten every boot: a permanent check that
  rename-over-existing works on LittleFS). Remove the `dataFileCheck()` test file code in
  `device/main.cpp` when P2 has real settings in `/data`.
  `SD.h` (Arduino) puts a global `fs` namespace in scope: in `device/*.cpp` write `pos::fs::`.
  The LittleFS partition is the one named `storage` in `partitions.csv` (not `spiffs`): 6016 KB.
  `device/device.h` declares the HAL classes; `Input::inject()` exists for P1-8.
  Verified on the device: SD overwrite via `pos::fs::writeFile` (rename over existing on FAT
  works, no `.tmp` left; temporary test code, removed), LittleFS overwrite (boot count 1..6).

- P1-8 notes: the card said 129600 bytes for the 4-bit frame; the right size is 259200
  (540*960/2), fixed in the card. The dump is taken from the Os canvas (what the firmware
  drew), until P1-3 moves it to the DisplayManager's shadow. `>shot` replies after earlier
  touches were processed, so its reply can arrive after replies of commands sent later: send
  it alone (device.py does). `Remote` lives in `src/core/remote.{h,cpp}` (parser
  `parseRemoteCommand` is pure, ready for unit tests in P1-5); `core/log.cpp` keeps the 50-line
  ring behind `>log`. Closed gap from P1-1: the touch marker path (input queue -> Os -> Fast
  refresh -> frame) is verified on the device with an injected tap; the GT911 translation is
  still only verified by a real finger (see Pending device checks).
  Limits: the dump shows what the firmware drew, not the physical panel (ghosting, waveforms).

- P1-3 device measurements (2026-11-15, `>set epd-timing on`, injected `tap 140 300`): touch
  logged at t=3260 ms; Fast push of the 35x35 marker region queued at 3261 and finished at 3507
  (246 ms push+waveform, ~247 ms from tap to done); the Text settle pass started at 3868
  (607 ms after the Fast push) and took 818 ms (done at 4686). Refresh log on the device
  (`tools/device.py refresh`): `F ... why=feedback`, `T ... why=settle`.
  The touch marker now has a gray ring (`drawTouchMarker`), so its Feedback pass has grays and
  gets a settle pass; a pure black dot would not (changed pixels pure black/white = no settle).
- P1-3 decisions: tests live in `tests/unit/` with PlatformIO's doctest runner (see
  SIMULATOR.md "Unit tests"); `Os::loopOnce()` no longer waits after pushes (only the boot
  frame waits); `>shot` takes the frame from `Os::frame4()` (simulator: emulated panel, from P1-4; else
  the manager's shadow); `>refresh` returns the manager's refresh log.

- P1-4 notes: after a tap the simulator frame shows the thresholded Fast pass (white ring around
  the black dot, `out/p14-fast.png`), 600 ms later the settled gray (`out/p14-settled.png`).
  The window was started and ran, but nobody looked at it (no screen access from here): the
  emulation was checked through `>shot` and the refresh log. Sim `changed` counts differ from the
  DisplayManager's (the panel counts value changes after thresholding).

- P1-5 notes: goldens are compared byte-for-byte; a changed pixel makes `make ui-test` print
  `DIFF <name>.png` (checked by moving the title down 1 px, then reverted). Goldens accepted
  after viewing: `demo-boot` (= the old `make shot` frame), `demo-tap-fast` (thresholded marker),
  `demo-tap-settled`. `make shot` still exists and is not part of `check` any more.
  `make test` runs the built test program itself because PlatformIO's doctest runner shows
  "SKIPPED" for a custom main.

- P1-6 findings: (1) M5GFX's BFF loader works for `lv_font_conv --format bin`; (2) it misreads
  sparse cmap subtables (offset relative to record vs payload) -> icons showed as boxes; fixed by
  rewriting the offsets in the generator (`patch_cmap`); (3) without a background colour glyphs
  have no anti-aliasing (see UI.md "Pitfalls"); verified now: caption, headline and icon areas
  contain all 16 gray levels. (4) `core/demo.cpp` includes `ui/` (core -> ui): temporary, goes away
  with the launcher app (P2-7). Fonts downloaded from the official releases: Inter 4.1 (rsms/inter),
  Literata 3.103 (googlefonts/literata), `lucide-static` 1.73.0 (npm); `lv_font_conv` 1.5.3 pinned.
  Firmware 1,354,379 bytes (25.8% of the 5 MB slot, under the 3 MB limit). Device frame
  (`out/device.png`) is pixel-identical to the simulator panel (`out/sim-m5.png`, `--name M5PaperS3`).
  Goldens updated on purpose (new look): demo-boot, demo-tap-fast, demo-tap-settled; new fonts-sheet.
  Not verified: how the text looks on the physical panel (sharpness, gray rendering).

- P1-7 notes: 31 unit tests (timers: order, ties, cancel, periodic, no-catch-up, 32-bit wrap;
  worker: off-thread job, delivery only on `runCompleted`, order, void, cancelled generation,
  wakeup, destructor). Device (injected taps): `tap 140 300` -> `[touch]`, Fast "Working..." at once,
  Text settle ~600 ms later, ~1 s later the worker result -> Fast "Done in 1.0 s" + settle; two
  parallel taps on two tiles both complete. Device frame after the job == simulator frame (pixel
  identical, `out/device-done.png` vs `out/sim-job-done.png`). Firmware 1,364,087 bytes. The demo
  tile job lives in `Os` (temporary, replaced by apps in P2). `Os::loopOnce()` still is what
  scripts and the remote step; the windowed simulator and the device use `Loop::iterate()`.

- P2-2 device (2026-11-15, `>set epd-timing on`, injected taps on the `widgets2` screen):
  Toggle tap at t=7443: pressed look Fast done at 7693 (250 ms after the touch), flipped look done at
  7954 (511 ms; the two passes run one after the other because the timing mode blocks on each);
  Button tap: pressed 262 ms, released 532 ms, then a Text settle pass (855 ms) because the label
  has anti-aliasing grays. Device frames after each step are pixel-identical to the simulator's
  (`out/dev-w2*.png` vs `out/sim-w2*.png`). Heap free after boot with the worker: ~242 KB.
  Not verified: how the pressed looks feel on the physical panel (thick borders, inverted rows).

- P2-3 device check (extra): `>set screen widgets3`, injected taps (Sheet open, dimmed-area close, Toast
  and its timeout): device frames identical to the simulator's after the same steps
  (`out/dev-w3-*.png` vs `out/sim-w3-*.png`; compare settled frames: a frame taken before the 600 ms settle
  differs by the anti-aliasing of the changed text, on both). Overlays and the toast timer work on the
  device. Not verified: how the dim hatch looks on the physical panel.

- P2-4 notes: the device now boots into the Shell with the (temporary, empty) launcher list; the
  demo launcher survives as `--app demo` for its goldens until P2-5. ArduinoJson 7.4.2 added.

- P2-5 device check (2026-11-15, `>set epd-timing on`, injected taps): launcher, then Read opened by a
  tap on its tile (t=13748): the pressed tile's Fast pass done 296 ms after the touch, the app screen
  (492 x 719 changed rectangle) Fast pass done 428 ms later (two serial waits because the timing mode
  blocks; ~0.72 s from touch to the app on screen), then one Text settle (1252 ms). Opening and closing
  Read, Home, WhatsApp, Mail and Settings: refresh log of 33 entries holds the boot Quality push only
  (F 21, T 11): **no Quality push for open/close**. Device frames (launcher, Read, Home, Mail,
  Settings, and the launcher again after back) are pixel-identical to the simulator's below the status bar
  (`out/dev-*.png` vs `out/sim-*.png`, `device.py diff A B 56`; the status bar shows live battery).
  Not verified: how a tap feels on the physical panel.

- P2-6 device check (2026-11-15): Settings opened from the launcher, every section opened and left by the
  back arrow with injected taps. Frames of the list, Wi-Fi, Display and Reading are pixel-identical to the
  simulator's below the status bar; Home Assistant shows "present" for HA_TOKEN and the key name only
  (`out/dev-s-ha.png`); Developer shows heap 229 / 353 KB, PSRAM 6.0 / 8.0 MB, refresh counters, log tail.
  The refresh log (50 entries, all F or T) shows no Quality push while navigating. The ghost limit change
  persists across a simulator restart (checked with two runs on the same `--data`). Not verified on the
  device: that the Display settings change real behaviour over time (ghost cleans), and "Clean screen now"
  (Quality flash) was not run on the panel.

- Navigation speed (2026-11-15, commit "Display: do not wait for press feedback"): tap to the new screen's Fast pass
  done went from 925 ms (Read tile; ~1 s for a row) to **605 ms** on the device (injected taps, epd timing
  mode); diff 107-229 -> 21-29 ms, render 116 -> 57 ms, push call 531 -> 88 ms. Cause and fixes (press
  look after 120 ms of holding, cheaper diff) in `docs/DISPLAY.md` "Navigation speed". Unit tests:
  `Pressable: a quick tap never shows the pressed look`, the diff span tests. Not verified: how the delayed
  pressed look feels under a real finger (a hold >120 ms shows it).

- P2-7 notes: `keyboard.script` types `Passw0rd!2x?` (capital, digits, symbols; layer switches handled by
  `Keyboard::tapsFor`); key presses stay under 6000 px except layer switches. Device (run after P3-1, injected taps): `widgets4`
  (keyboard, 3 keys typed) and `widgets5` (TextBlock, page 1 and a tap to page 2) frames are pixel-identical to the
  simulator's; the first TextBlock page layout took 222 ms on the UI thread (a whole chapter must be paginated on the
  worker). Not verified: how typing feels under real fingers. The reader
  and the inbox will use `TextBlock`; a real book's font-size change relies on `pageIndexOf` (tested with a fake
  metric and with the real fonts on generated text).

- P3-1 (2026-11-15): device power HAL. Light sleep (timer): works, 5 s -> 4992 ms, 10 s -> 9991 ms, cause timer.
  **The USB serial session does not survive light sleep** (handle dead afterwards, input during sleep lost, no early
  wake from USB). Power off with RTC alarm (`powerOff(20)`): port gone at once, back after 20 s, clean fresh boot.
  Settings > Developer has Sleep test and Power off (sheet). Side-button detection by a running program: not possible
  (no `BtnPWR` for the PaperS3 in M5Unified; the button is wired to the PMS150G).

- P3-4 (2026-11-15): `PowerPolicy` (idle sleep + auto-off). Device: on USB, idle 12 s -> 0 light sleeps, device
  reachable; with `>set usbsleep 30` (test aid: allow sleep on USB for 30 s) the device slept once for 30 s (asked
  30000 ms, woke after 29992 ms: the sleep ends exactly at the next deadline), then answered `>log` again.
  Unit tests: `decideSleep` table (every condition), sleep ends at the next deadline and is capped at 60 s, auto-off
  limits (normal, reader, 0, USB), state saved and `lastApp` remembered, USB rule and its flag.

- P3-2/P3-3/P3-7 (2026-11-15, device with the owner's card: keys `HA_TOKEN(183) WIFI_SSID(19) WIFI_PASSWORD(12) HA_URL(26)`,
  checked after a reboot because the card was swapped): **joined the home network** (an SSID with `/` and `.` in it, 2.4 GHz channel 3,
  RSSI -52..-56). Scan path: 5298 ms (scan + join); fast path (stored BSSID/channel): **1245 ms**; Wi-Fi went off after 21 s idle
  (linger 20 s). SNTP set the clock after the first connect (status bar shows 15:45 CET with the Wi-Fi icon). HTTPS
  `GET <HA_URL>/api/` with the token (Settings > Home Assistant > Test connection): **HTTP 200 in 2.5 s**, reply "API running.";
  heap 200 KB idle -> 150 KB Wi-Fi up -> 104 KB during the TLS request (worker stack 16 KB was enough). The firmware grew to 2.30 MB
  (Wi-Fi + TLS; the budget is 3 MB). `tools/device.py status` shows key names and lengths, networks, Wi-Fi, heap and a leak
  count; the log ring never contained any secret value (0 occurrences).
  Not verified: networks beyond the first (only one is on the card); joining a network from the Wi-Fi screen on the device with
  the keyboard (only in the simulator); a failure path on the device (no known network in range); behaviour on 5 GHz-only.

- Clock and first-request fixes (2026-11-15): (1) the status bar showed UTC because Arduino's `configTime()` (called by the SNTP
  sync) overwrites the `TZ` variable with UTC; `Net::syncTime` now keeps the zone and the net service re-applies
  `net.timezone` when it applies a synced time; the RTC stays UTC. Unit test: 2026-11-15 15:51 UTC = 16:51 CET, a summer date, the
  March changeover. Device: status bar 16:55 vs the Mac's 16:55:13. (2) First HTTPS request 9.5 s -> **2.0-2.6 s cold, 34 ms on the
  kept-alive connection**: the cause was the UI thread blocking in `Serial.print` (USB CDC with no reader), plus SNTP inside the
  connect path; breakdown table in `docs/POWER.md`. TLS handshake ~0.6 s at 240 MHz.
- **Firmware size and headroom**: 2,309,779 bytes = 2.20 MB = 44% of the 5 MB slot; the budget in ARCHITECTURE.md is 3 MB (3,145,728), so
  **~0.8 MB of headroom** remains for the reader, Home and inbox apps. The Wi-Fi/TLS step (P3-2/3/7) added ~0.80 MB (1,488,831 ->
  2,303,883): the Arduino WiFi + lwIP + NetworkClientSecure/mbedTLS + HTTPClient stack with the built-in CA bundle (the bulk, roughly
  0.7 MB by symbol sizes; the exact split is not measured), our net service and Wi-Fi/Home Assistant screens (~60 KB, estimate). Fonts
  are 0.45 MB, M5GFX/M5Unified ~0.2 MB. The reader (miniz ~12 KB, XHTML, pagination, library) should fit in ~150 KB.

- **Reader, after P4-6 (2026-11-16)**: (1) no EPUB of 1 MB is on the card (the largest of the six test books is 609 KB), so the
  card's "cold open of a 1 MB EPUB" is an extrapolation: 20-25 s once, from 6-9 ms per page; put a larger book on the card to
  measure it. (2) A first open costs 10-16 s, most of it layout (text measuring 40-55 % of that): the place to look if that is
  to get shorter. (3) A TXT book is one chapter: its whole text is loaded and parsed on every open (The Time Machine, 181 KB:
  0.6 s after the book is ready); a 1 MB TXT would take several seconds per open unless TXT is split at import. (4) The worker
  is one queue: a job a tap waits for must not be queued behind a slow write or a decode (the three cases found are fixed; the
  cover write of a first open still delays whatever is tapped in the 2-3 s after the first page).

## Log

- 2026-11-15: Phase 0. Research (M5Stack docs, M5GFX/M5Unified 0.3.0 source), stack chosen,
  simulator + firmware spike built, factory flash backed up, spike flashed, timings measured,
  documentation set written. First commit `1bb8a02`.
- 2026-11-15: User answers. Backend redesigned around Home Assistant integrations (WhatsApp
  add-on, Proton via Bridge + IMAP), non-admin access without `/api/template`, secrets from
  `/sd/paperos_config`. SD card, touch and the token file verified on the device. Open books
  fetched for the simulator; task P4-0 added to push them to the card.
- 2026-11-15: P1-1. `src/hal/*.h`, `core/log.h`, `core/fs.{h,cpp}`, `core/os.{h,cpp}`; mains
  build the Platform. `make check` passes, `out/shot.png` byte-identical to before. Device
  flashed and checked from the serial log: boots, board 19, `[boot] full quality refresh 1363ms`,
  `[beat]` and `[sd]` every 5 s (config key HA_TOKEN, length only). Touch dot not re-tested.
  `pos::fs` checked with an ad-hoc host test (resolve, atomic write/overwrite, listDir, mkdirs).
- 2026-11-15: P1-2. Device HAL split into display/input/clock/storage; storage mounts SD and
  LittleFS and reports sizes. Checked from the serial log over several reboots/reflashes.
- 2026-11-15: P1-8. Remote control over USB serial and simulator stdin. Device frame
  (`out/device.png`) identical to the simulator's (`out/shot-m5.png`, `--name M5PaperS3`); an
  injected `tap 140 300` shows the marker and logs `[touch] x=140 y=300 fast refresh 257ms`.
- 2026-11-15: P1-3. DisplayManager with fast-then-settle, ghost budget, refresh log; unit harness
  and `make test`; device measurements above.
- 2026-11-15: P1-4. Simulator e-ink emulation, refresh log, input and clock split into files.
- 2026-11-15: P1-5. Script runner, golden screenshots, gallery, make targets, more unit tests.
- 2026-11-15: P1-6. Fonts and icons pipeline, text API, demo with Inter and Lucide.
- 2026-11-15: P1-7. Event loop idle step, timers, worker; demo job on sim and device. Phase 1 done.
- 2026-11-15: P2-2. Button, IconButton, Toggle, Badge, ListItem, Tile, Pager; three test screens with goldens.
- 2026-11-15: P2-3. Segmented, Stepper, LevelBar, EmptyState, Sheet, Toast; overlays in Screen; widgets3 test screen.
- 2026-11-15: P2-4. App framework, Shell, store/cache, chrome widgets; ui::Screen renamed ui::Root.
- 2026-11-15: P2-5. Launcher grid, placeholder apps, app template, new-app.sh; demo screen removed.
- 2026-11-15: P2-6. Config, secrets, time zone, Settings; ui-test uses one data root per script.
- 2026-11-15: navigation speed: press look delayed 120 ms, cheaper diff; 925 -> 605 ms to a new screen.
- 2026-11-15: P2-7. Keyboard, TextField, TextBlock with pagination; type / type-max script commands.
- 2026-11-15: P3-1. Device power HAL: battery average, light sleep with wake cause, RTC power-off; Settings Sleep test / Power off.
- 2026-11-15: P3-4. PowerPolicy: light sleep when idle (not on USB by default), auto-off on battery.
- 2026-11-15: P3-2, P3-3, P3-7. Net service (connect on demand, fast path, linger, SNTP), multi-network secrets, HTTP via ctx.http, Wi-Fi settings, Home Assistant test, `>status`.
- 2026-11-15: clock in local time (TZ kept across SNTP); first HTTPS request 9.5 s -> 2.0-2.6 s (UI thread no longer blocks on the USB log).
- 2026-11-15: P4-0. `>put/>ls/>rm` (CRC-checked, `.part` then rename, only `/sd/books` and `/sd/paperos`), `tools/push.py`, `make books`. Six books (2.16 MB) pushed to the card in 15.9 s = 121-144 KB/s; `>ls /sd/books` sizes match the files; a second push skips them.
- 2026-11-15: P4-1. Reader app (page screen full screen, touch zones, footer, progress), TXT import, page index on the worker, `restart` / `expect-state` / `page-through` script commands.
- 2026-11-15: P4-2. Library screen (scan on the worker, continue card, 7 rows per page with Pager), `tests/ui/library.script` with nine books.
- 2026-11-15: P4-3. `lib/miniz` (tinfl only, MIT), `core/zip`, `apps/reader/xml` tokenizer, `apps/reader/epub` (container, OPF, nav, NCX), EPUB meta in `meta.json`; fixture EPUBs and damaged-file tests; the five real EPUBs parse.
- 2026-11-15: P4-4. XHTML to marked text, EPUB open (convert, paginate from memory, index file), "Preparing book..." with percentage and cancel, chapters converted on demand, cover in the background; `tools/books-check.sh` (six real books to their last page), `tools/books-device.py`. Device: cold index of Pride and Prejudice (609 KB EPUB, 1542 pages) 15.3 s, cached open 0.22 s.
- 2026-11-15: P4-5. Reader menu (text size S/M/L/XL, go to by 10% and by page, contents, clean screen), cover thumbnail on the library card (decoded on the worker, box-filter scaled, `Hint::Image`), `Row` measures flex children at their own width. Goldens: `tests/ui/reader-menu.script`.
- 2026-11-15: P3-6 (simulator part). Sleep screen (`core/sleep_screen`, `App::sleepInfo`), resume of the last app and the open book after power-off, `power-off` script command, `ui/image` (cover decoding shared by library and sleep screen).
- 2026-11-15: P5-1. `core/ha` (HaClient: get, post, callService, getMany, error mapping), `ctx.ha()`, Settings test uses it.
- 2026-11-15: P5-2. `apps/home/home_model` (home.json parser, default config with the full Avond scene, state parser, light views with group counts, states cache, call bodies).
- 2026-11-15: P5-3, P5-4, P5-5 (simulator part). Home app (Lights with scenes, member sheet, Climate, More), optimistic actions with revert, cache first, foreground polling; goldens `home-*`.
- 2026-11-15: P6-4, P6-5, P6-6. `core/text_clean`, Inbox app for WhatsApp and Mail (grouped chats, detail pages, full-text fetch, mark all read, seen marker, badge file), `Screen::setAction`; goldens `inbox-*`, `mail-*`.
- 2026-11-15: P7-2 run on the device (five scripts identical to the goldens), no light sleep within 10 minutes of a USB host (`kUsbHostHoldMs`), `tools/device.py` cannot hang any more (write timeout, `--limit`), `inbox-error` test flake fixed (longer wait), `device.py scripts` crash on `fonts` fixed; OTA and book upload protection options written as open questions 4 and 5.
- 2026-11-15: P7-5 scheduled refresh: `power.refreshHours` (Settings > Power), `Os::powerOffForSleep` / `takeScheduledWake`, `apps/inbox/scheduled_refresh`, sleep screen summary line, `/data/refresh.log`, script commands `timer-wake` / `expect-off`, `>poweroff N [M]`; goldens `sched-*`; four RTC wakes measured on the device.
- 2026-11-15: P7-6 quick panel (`core/quick_panel`, `Shell::sleepNow`); Home light sheet shows the brightness value.
- 2026-11-15: launcher summary line (`summaryLine`, golden with and without data).
- 2026-11-15: P6-3 (WhatsApp half, done in parallel): `ha/paperos_inbox.yaml` synced from the Home Assistant repo; BACKEND.md event fields and feed mapping corrected; D20; add-on security facts.
- 2026-11-15: P3-5 preparation: `core/power_log` (`/data/power.csv`, every 10 min), Developer screen shows battery, time since boot and the power log switch, `>cat` / `tools/device.py cat`.
- 2026-11-15: P7-1 Display lab screen (ramp, checker / white ghost test, text samples, counters); Developer screen: Display lab row, memory in one row.
- 2026-11-16: P6-7 clipboard and WhatsApp sending (D21), steps 2-5: TextField sheet, multi-line fields and a character limit; disabled primary
  buttons are drawn as an outline (gray text on a gray fill was unreadable); `script.paperos_whatsapp_send` + `sensor.paperos_whatsapp_sends`
  in the package and `tests/ha/check-send-script.py`; config `inbox.whatsapp.favorites`; Reply / New message / compose / confirmation;
  `apps/inbox/compose.h`; device test aid `>set favorite`. Not installed in Home Assistant, nothing sent.
- 2026-11-15: P7-7 camera tooling: `tools/cam.py` (devices, shot, video, calibrate, compare, around, selftest), `make cam-shot` / `cam-video` / `cam-compare`; per-run registration, per-image normalisation, no goldens (the set-up moves); camera-checkable items moved to "To verify with the camera". Waiting for the webcam to be plugged in.
- 2026-11-16: D22. Home Assistant not trusted with message text: inbox through an end-to-end encrypted companion Android app (planned,
  P6-8 to P6-11). WhatsApp Gateway (logged out first) and Proton Mail Bridge add-ons removed with the `whatsapp` integration and the
  FaserF repository; package cut to `script.paperos_inbox_clear` + recorder exclusion (home-assistant `ebc133d`); a 404 feed shows
  "Not set up yet" and drops the saved list and badge; `tests/ha/check-send-script.py` removed with the script.
- 2026-11-16: P6-8 to P6-10. Companion protocol v1 (`docs/COMPANION.md`, vectors), relay in Home Assistant (`e04d613`: push/ack scripts,
  three recorder-excluded queue sensors), `core/companion_crypto` (libsodium) and `core/companion`, Settings > Home Assistant > Phone
  (QR pairing, `ui/qr`), inbox / scheduled refresh / replies / Somdoku on the relay, fixtures encrypted with pyca/cryptography; device
  round trip with a scripted test phone through live Home Assistant.
- 2026-11-16: P7-7 with the real webcam (C922): capture server in Terminal (the editor gets no camera permission), wall-clock stops, `compare --ref`, `around` reports the ghost a Clean removed; Display lab ghost test and Clean flash measured.
- 2026-11-16: P7-7 uvc-util built (`~/.local/bin`), `cam.py lock` / `unlock`, video modes chosen within the USB budget (30 fps at 640x480); the Ramp measured in focus.
- 2026-11-16: P6-12 clipboard sync with the phone (D23): `clip` in COMPANION.md 7, `clipboard::Sync` (`apps/inbox/clipboard_sync`),
  push on copy, pull before a paste, the relay fetch keeps rx without overwriting a newer tx, Settings switch; Android: share target,
  Quick Settings tile, app button and switch. 229 unit tests, 172 goldens (new `textfield-phone-menu`, `textfield-phone-pasted`),
  35 JVM tests. Not on the phone or device yet.
- 2026-11-16: P7-7 reader ghosting with the camera (1/10/30 turns at M, 10 turns at S/L/XL, one page turn timed); finding: no automatic clean while reading; `device.py` fails fast after the first answer.
- 2026-11-16: reader page turns as `Hint::Page` (no settle sweep, Quality clean every 10th page, `display.pageCleanEvery`); Page commits before Content; 23 reader goldens now show the black/white page; waits lengthened in `library` and `reader-menu` scripts (footer settle); flashed and measured with the camera.
- 2026-11-16: clean gesture: swipe up from the bottom 60 px = one full-screen Quality pass on every screen (`Shell::onGesture`, `kCleanEdgeH`); unit test; on the device with the camera.
- 2026-11-16: going home drew the icon wells in a second pass (Fast thresholded gray(13) to white, the settle 0.6 s later brought them in). A Screen pass with flat gray areas is now one Text push (`why=screen-gray`, DISPLAY.md "As implemented"); 2 unit tests, golden `launcher-back-first` (wells in the first push), `lab-*` goldens: push counters only. Simulator only; device check pending.
- 2026-11-16: tap latency (DISPLAY.md "Tap latency", D24): `[lat]` trace + `tools/latency.py`; fast text path (byte-identical to LovyanGFX); M5GFX patched at build time (`tools/patch_m5gfx.py`: tiled table-driven frame buffer write, finished rows skipped; `>set epd-selftest on`); reader prefetches the next page; Settings write the config a second later. Page turn 516 -> 199 ms, screen 807 -> 408 ms, small tap 52 -> 7 ms (finger up to the driver).
- 2026-11-16: D25: the Arduino core / ESP-IDF 5.5.5 built from source (`custom_sdkconfig`): 64 KB data cache, 64-byte lines, PSRAM initialised before global constructors. Screen change during a settle 408 -> 306 ms. Pitfalls (penv PlatformIO 6.2.0, SPIRAM_BOOT_INIT) in D25.
- 2026-11-16: P7-9 the word game (D26): `apps/wordgame` (game logic, word lists + `tools/gen-wordgame-words.py`, game and statistics screens, score to the clipboard); 8 unit test cases, 13 new goldens, 9 launcher goldens with the pager bar. Simulator only.
- 2026-11-16: P7-9 the word game fetches more accepted guesses weekly from woordscore.nl (second part of the list only), cached for offline use; 3 unit test cases, `wordgame-list.script` with a fixture, live fetch checked in the simulator (5616 words).
- 2026-11-16: simulator: the Fast pass is the panel driver's ordered dither instead of a 50 % threshold (grays show as dots at once, as the driver writes them); 55 goldens that show a Fast image changed. The word game: backspace one push (ink frame for empty tiles), used-up keys gray(10).
- 2026-11-16: The word game: `kName` set to "Woordle" by the user (own edit); the share text unit test follows the constant, 15 goldens with the title or the tile regenerated; launcher tile label from `kName`.
- 2026-11-16: D25 120 MHz flash + PSRAM: soak 20 min + 10 min with page turns clean (29.4 -> 34.4 C), temperature in boot log and heartbeat, restart guard at 12 C drift; page turn 140 ms, screen change 190 ms, small tap 5 ms (finger up to the driver). Full flash backed up before (`backup/before-120m-2026-11-16.bin`).
- 2026-11-16: Display lab: fifth pattern "Fast" (the gray ramp left in its Fast state, `Hint::Page`) to check the driver's Fast dither with the camera; golden `lab-fast-ramp`, the five lab goldens with the wider control.
- 2026-11-16: Fast pass dither confirmed on the panel with the camera (Display lab "Fast"); the Fast ramp is upside down so it is pushed also after Ramp; golden `lab-fast-after-ramp`.
- 2026-11-16: P6-12: a copy whose `clip` push failed offline (or timed out, or a server error) is resent when Wi-Fi next comes up (`NetService::onConnected`, `clipboard::markUnsent` / `unsent`, `/data/clipboard_unsent` holds only the copy time); checked in the simulator with `wifi off` / `wifi on`. Not on the device.
- 2026-11-16: tap latency round 3: M5GFX patch 3 (frame buffer write on both cores), the diff updates the shadow in the same pass, the reader saves its position a second after the turn. Page turn 140 -> 112 ms, screen change 190 -> 150 ms. Noted: with a book open, internal heap free drops ~111 -> ~78 KB (chapter text and layouts as small strings), back to ~101 KB after leaving.
- 2026-11-16: D27 memory: allocations to PSRAM (`SPIRAM_MALLOC_ALWAYSINTERNAL=0`, 32 KB internal reserve), mbedTLS in PSRAM, core dumps off, log ring and serial buffer on the heap; `>set memreport` and `tools/memory.py`. Internal free idle 108 -> 141K, Home's lowest during TLS 18 -> 69K, Woordle 18 -> 126K; latency unchanged.
- 2026-11-16: The word game: "Woordenlijst bijwerken" on the statistics screen (the list fetch by hand; shared `fetchList`), goldens `wordgame-list-updated`, `wordgame-list-failed`; device memory numbers of the app recorded.
- 2026-11-16: fixed a reboot during HTTPS downloads (task watchdog: HTTPClient's read loop spun on core 0 while the TLS client had no data; `YieldingClient` in `platform/device/net.cpp`). Woordle's list fetch on the device: 5616 words, internal free lowest 70K; Home unchanged (lowest 69K).
- 2026-11-16: The word game's list fetch ran on the device (5616 words, heap lowest 70 KB); it exposed a watchdog reboot in HTTPS downloads on the worker, fixed by the memory pass (54ad248).
- 2026-11-16: The word game on the device (taps, frame dumps, list update, one camera still); statistics line shortened after it was cut off there. A played guess not run (today's puzzle).
- 2026-11-16: The word game: today's puzzle played on the device with injected taps (marks, resume, a fetched-only word, won screen, copy, statistics); camera stills of the marks on the panel.
- 2026-11-16: the toast is an ink band over the status bar instead of a button-like pill at the bottom; 22 goldens; unit test taps through it at the top.
- 2026-11-16: names in English: the word game is `wordgame` (directory, namespace, app id, tests, goldens, tools, docs); old saved data adopted on first open.
- 2026-11-16: `>shot` streams between loop passes (no 1 s UI stall, 0.5 s per dump); the reader loads the next chapter two pages ahead (a boundary turn could wait ~1 s while the worker shared core 0 with the waveform task); 60 turns: median 105 ms, slowest 159 ms.
- 2026-11-16: reader footer (chapter, "12 / 340") is part of the page's push (`Hint::Page`) and drawn in ink: the user saw the page number flicker more than the text (it got a Fast pass and then its own settle sweep); gray caption text breaks up in the Fast pass, so ink. One push per turn on the device.
- 2026-11-16: the user keeps the reader footer in ink ("Keep black") over gray with its own settle pass.

- 2026-11-16: live view (`make view`, `tools/view.py`): the device in a browser page, click = tap, hold = long press, drag = swipe; checked on the device (reader page forward and back, ~760 ms from click to new frame) and in the simulator (tap, long press on back, swipe); `--lan` with a random key for a phone. `device.py` opens the port exclusively (a second tool refuses to start instead of sharing replies) and `unpack4` is a table lookup.
- 2026-11-16: `make check` was red since 5db1f1a (found while adding the live view): `reader.script` compared the screenshots of page 7 before and after `restart` byte for byte. Screenshots show the emulated panel, and since the footer goes out with the page (Fast, no settle pass) 11 footer pixels outside the last Fast pass's box (x 497 and the foot of the slash) still held the grays of the last Quality clean, while after a boot the whole footer went out in one Fast pass. The drawn page is the same: new script command `expect-frame-equal A B` compares what was drawn (the display manager's shadow) and `reader.script` uses it; `reader-last` golden accepted (footer in ink: the script stopped before it in 5db1f1a). No change to the reader or the display code.
- 2026-11-16: P4-6 reader on the device. Measured: page turn 109 ms (median of 30, slowest 150 ms), cached open 30-230 ms with the
  text on screen in 0.7-1.4 s, first open 9.8-15.7 s (four EPUBs of 312-609 KB), ghost 1.27 / 1.15 / 1.11 / 1.22 / 1.14 % after
  1 / 9 / 10 / 29 / 30 turns (flat: the floor of the session is 1.1 %), defaults kept. Fixed: the first page waited 1.9 s behind
  the cover write and dropped taps (now 0.3-0.4 s); resuming into the open book waited behind the library scan and cover decode
  (2.45 s -> 0.14 s); the library card's cover was decoded on every return (a book tapped then opened 2-3 s late). Found on the
  way and fixed: a log line printed while a frame dump streamed out landed inside it ("no reply to '>shot'"): the device holds its
  serial log while a reply leaves (`RemoteHost::replyLeaving`, unit test; 80 dumps in a row whole). Not measured: a 1 MB EPUB.
- 2026-11-16: P7-11 weather app (simulator): `src/apps/weather/`, Google Weather through `weather.thuis` and `weather.get_forecasts`;
  POST fixtures may carry a body hash (SIMULATOR.md); 4 new goldens. Device check pending.
- 2026-11-16: P7-11 weather, second pass: three pages in a Pager (now / today and tomorrow in three-hour blocks / ten days),
  sunrise and sunset from `sun.sun` (Lucide `sunrise`, `sunset`), own screen for a refused token, UI scripts for 401 and a
  missing entity, D29. 9 weather goldens.
- 2026-11-16: P7-11 on the device: the branch's firmware flashed, live `weather.thuis` read (10 days, 24 hours), three pages
  dumped, memory measured (`tools/memory.py` has a `weather` scenario now). The device runs the weather branch: no agenda.
- 2026-11-16: P7-11 two places (Thuis, Office): switcher, settings keys, a saved copy per place, fixtures for
  `weather.office`, goldens `weather-office`, `weather-office-hours`. Flashed: Thuis reads, the Office tab shows the missing
  entity screen (404 from the live instance: the missing-entity screen is thereby checked on the device too).
- 2026-11-16: P7-11 Office added in Home Assistant (Google Weather, a second location; nothing in the config repo): the
  Office tab reads on the device. An amount under 0.05 mm is no longer shown as "0.0 mm".
- 2026-11-16: P7-11 rain graph: the minute forecast sensor read with the place, two-hour bar graph on the first page,
  golden `weather-dry` (also the one-place layout). On the device: `read: partlycloudy, 10 days, 24 hours, 23 rain blocks`,
  the live sensor parses, the page says "Dry for the next 2 hours" (18:47). The graph itself with live rain was not seen on
  the device yet (no rain in the forecast): simulator only.
- 2026-11-16: D31 firmware size pass: 2.85 -> 2.31 MB (-564 KB). `<sstream>` replaced in `core/remote.cpp`, exceptions off
  (ESP-IDF and `-fno-exceptions`), common certificate bundle, TLS client only, no SoftAP (stub for Arduino's WiFi library), no
  WPA Enterprise, `CORE_DEBUG_LEVEL=1`, no mbedTLS error texts, silent assertions; `CONFIG_LWIP_IPV6=n` does not compile; our
  `-O2` never had an effect (the build script's `-Os` wins). Flashed: boot, HTTPS to Home Assistant, latency unchanged.
- 2026-11-17: Code review of the whole code base (six reviewers in parallel, then verified by hand) and refactor, on the
  user's "review code, refactor where needed. Good quality, no copy-paste, good api's. solid". Five commits. (1) One copy
  of the helpers: `core/utf8`, `core/strings`, `core/crc32`, dates and their text in `core/timefmt`. (2) Bugs in core and
  platform: `>rm /sd/books/` deleted the library (path spellings), a serial line over 128 bytes ran cut short, a failed
  upload or MOVE lost the old file, `<header>` was skipped as `<head>`, navigation ran through a closed app's Nav, a
  damaged page index was trusted, the log ring raced. (3) UI kit: `openSheet` / `confirmSheet`, `PagedList`,
  `Widget::removeAll`; badge layout, Pager clamp, toast timer, emoji glyph cache. (4) Apps: launcher badges after a page
  turn, one Home Assistant error screen, the daily games' shared module, dead code out. (5) Tools: failures give a failing
  exit code (`push.py`, `fetch-books.sh`, `device.py rm/status/log`), `new-app.sh` refuses names that break sed and never
  overwrites, simulator runs of the test scripts are bounded (`tools/sim-run.sh`). **Left from the review, not done**
  (judged lower value or needing a decision): splitting `settings_app.cpp` (1040 lines), `book.cpp` and
  `wordgame_app.cpp` by concern; one shared serial module for the Python tools (`device.py` is imported by five of them,
  each with its own `ok()` / tap helpers); shared fake HAL and temp-card fixtures for the unit tests (17 and 5 copies);
  file reads and JSON parsing on the UI thread in Home, the inbox and the launcher (APPS.md rule 1); `HaError` derived
  from message text (`core/ha.cpp`); the unix-time "seen" marker stored as an `int` (2038); a reply of 900 non-ASCII
  characters does not fit the 1000-byte envelope and is refused ("does not fit") instead of counted in bytes while typing;
  `device.py` resends input commands (a slow device can run a tap twice); the inbox's "Clear caches" also deletes the
  decrypted notifications. Not flashed (no device attached): Pending device checks.
- 2026-11-17: Two things the owner asked for after the review. (1) "find redraw bugs. I see allot of flashes when
  navigating through the OS. Especially Settings": the refresh log of a walk through Settings shows a Fast pass and a Text
  settle over 78 % of the screen per navigation; the driver source shows why both flash the whole area (a pixel is driven
  again when the mode of the request differs, white background included). Patch 4 of `tools/patch_m5gfx.py` leaves
  unchanged pixels alone (`>set epd-keep off` = stock); a toast is no longer a "screen change"; a list that is filled
  again redraws only the rows that changed. Not on a panel yet. (2) "Wifi networks do not refresh. It only does a scan
  once but it doesn't find/show all": the scan page lists every network (paged), scans three times and adds what is new
  at the end, with a scan-again action; `ScanList` (unit test), `wifi crowd N` in the simulator, goldens `wifi-scan-*`.
- 2026-11-17: Device run after the review (the owner put the device back on USB under the camera). Flashed; serial path,
  delete guard, file server on the card, power-off resume, Wi-Fi scan page: all pass ("Done on the device 2026-11-17").
  The flash patch measured with the camera: the unchanged white background darkened 66-82 % of contrast in every settle
  pass with the stock driver rule, under 0.5 % with patch 4; grays and ghost not worse. `cam.py around` now prints that
  number per push.
- 2026-11-17: Camera calibration target: screen `camcal` and `tools/cam.py target`
  (`make cam-target`), DISPLAY.md "Calibration target". Flashed and run under the camera: panel found to 0.21 px, no lens
  correction needed; it showed that the picture is dark (white at 44-63 of 255) and that a reflection lies on the left
  edge of the panel (ink reads up to 0.28 there). Both are for the set-up (more light, move what is mirrored), then
  `make cam-target` again; gray numbers were worth about 0.11 in that light.
- 2026-11-17: The camera in the 3D printer (the user cannot change the light there). `cam.py lock` now measures:
  exposure 33 ms and the gain that puts the panel's white at 185 of 255 (the C922 reports its defaults in automatic mode,
  so the old lock froze a dark picture: white at 44-63, now 166-195). The target's dots give a map of what ink reads
  (28-67 % of the local white: a reflection on the left edge); `compare` and `around` use it for gray levels for an hour.
  A single gray patch is good to about 0.06, the mean of the target's three places to 0.02 (DISPLAY.md "Calibration
  target"). Start of a camera session: `cam.py lock`, then `make cam-target`.
- 2026-11-17: File reads on the UI thread (the review's item). Measured first: opening an app cost 65-114 ms before
  the first pixel, leaving one for the launcher 134-354 ms: the launcher read 16 small files (badges twice, the reader's
  store, the agenda), ~9 ms each on the internal flash. Not made asynchronous (that is a second pass on the panel):
  `core/fs` keeps small `/data` files in memory, every write, append, delete and rename through it keeps that true.
  On the device: launcher 15 ms before the first pixel, tap to panel 137 ms (was 255-476); opening WhatsApp 54-88 ms,
  Mail 64 ms. `ctx.background(job)` / `Worker::post` for work that must run even when the app closes (it was queued
  with `ctx.async` and skipped then: Wi-Fi power save after Files over Wi-Fi, Home's saved copy, the book cover).
  APPS.md "Threads: what an app has to know". Still on the UI thread: the inbox parses and cleans its saved
  notifications when it opens (part of those 54-88 ms).
- 2026-11-17: Opening the inbox. Two changes, one measured gain. (1) Notifications are cleaned when they arrive
  (`companion::cleanNotif` in the relay read, on the worker) and stored clean, not cleaned every time WhatsApp or Mail
  opens; an older store is cleaned on load. Measured on the device: no difference (53-54 ms before the first pixel, the
  store held few messages). (2) The real cost: the screens called `companion::loadPairing` twice on the UI thread to
  ask whether a phone is paired, and that derives the session keys (two curve operations). `companion::pairedPhone()`
  reads the stored pairing only; used by the inbox, Agenda and Settings. On the device: WhatsApp and Mail 18-19 ms
  before the first pixel, tap to panel 179-197 ms (was 221-238). Still with keys on the UI thread, on a tap: "Mark all
  read", sending a reply, the clipboard push.
- 2026-11-17: Launcher badges. On the device: the phone is paired, Mail held three
  messages, no badge. An app wrote only its own badge, and only after marking everything seen, so it was always 0;
  the other app's badge was never touched. Now every read of the relay writes both (`fetchRelay`), and a badge that
  does not change is not written to the flash again. Simulator: after WhatsApp read the relay, Mail shows 5 and the
  launcher "5 unread" (golden `inbox-badges-launcher`). Flashed; not seen on the device (no unread message at hand).
  Open: nothing reads the relay while the device is on and idle (the scheduled wake is off on the user's device).
- 2026-11-17: The launcher reads the phone's relay itself: when it comes up and the last read
  or try is over ten minutes old, 1.5 s later, in the background (apps/launcher.md "Fresh badges"). On the device
  after a boot: Wi-Fi 1.3 s, the read 1.2 s, only the Wi-Fi icon changed on the panel; no second read on the next
  return to the launcher. Golden `agenda-launcher-page2` re-accepted. Not seen with a real unread message yet.
