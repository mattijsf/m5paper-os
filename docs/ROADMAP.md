# Roadmap

Work proceeds task by task. Each task has an id (`P<phase>-<n>`) that code comments and other docs
refer to. `STATUS.md` says where things stand; `DECISIONS.md` says why the stack is what it is.

Legend: `[x]` done, `[ ]` open, `[~]` partly done, `[-]` dropped (kept listed so it is not proposed again).

## How to work a task

1. Read `STATUS.md`, then the task card, then every doc under **Read**.
2. Implement only what the card says. If something outside the card looks necessary, note it in
   `STATUS.md` under "Notes for later tasks" instead of doing it.
3. Run the card's **Done when** checks and `make check`. For UI work, render the screenshots
   (`make shot`, `make ui-test`, `make gallery`) and look at them.
4. **Device check**: run it if a device is attached (`ls /dev/cu.usbmodem*`), with the serial log,
   frame dumps and injected input (`tools/device.py`), and the webcam for what the panel physically
   shows (`tools/cam.py`). If no device is attached, add the check to "Pending device checks" in
   `STATUS.md`. Never report a device check as passed without running it.
5. Tick the box here, update `STATUS.md` (state, measurements, surprises), fix any doc the work made
   wrong, and commit with a conventional commit message (`feat(reader): ...`, `fix(home): ...`,
   `docs: ...`).
6. If a card's assumption turns out false (API missing, measurement far off), stop, write what was
   found in `STATUS.md`, and propose a change in `DECISIONS.md`; do not improvise a different
   architecture.

---

## Phase 0: Foundation

- [x] **P0-1 Toolchain and skeleton.** PlatformIO with `sim` and `papers3` environments, partitions, Makefile, shared code drawn by both platforms.
- [x] **P0-2 Headless simulator.** `program --headless --shot` writes a 16-level PNG.
- [x] **P0-3 First flash and measurements.** Factory image backed up; hardware facts and refresh timings in `HARDWARE.md` and `DISPLAY.md`.

## Phase 1: Platform core

Shared code runs an event loop, draws through the DisplayManager, and the simulator runs scripts
and compares screenshots.

- [x] **P1-1 HAL interfaces, logging, file paths.** `src/hal/*.h`, `core/log` (`POS_LOG`), `core/fs` (atomic writes), `core/os`; no platform header outside `src/platform/`.
- [x] **P1-2 Device HAL: display, touch, clock, storage.** `src/platform/device/`: panel push, GT911 touch events, RTC, SD at `/sd` and LittleFS at `/littlefs`.
- [x] **P1-3 DisplayManager.** `core/display_manager`: shadow buffer diff, hint-to-mode table, fast-then-settle, ghost budget; unit tests with a fake display.
- [x] **P1-4 Simulator HAL: e-ink emulation, refresh log, input, clock.** Panel emulation with the three modes and residue, `out/refresh.log`, mouse as touch, fake clock in headless mode.
- [x] **P1-5 Script runner, golden tests, unit test harness.** UI scripts (`tests/ui/*.script`), goldens, doctest unit tests, `make test` / `ui-test` / `ui-accept` / `gallery` / `check`.
- [x] **P1-6 Fonts and icons pipeline.** Inter, Literata and Lucide baked by `tools/gen-assets.sh` into `src/gen/`; `ui/theme`, `ui/text`, `ui::icon` (D12).
- [x] **P1-7 Event loop, timers, worker.** `core/loop`, `core/timers`, `core/worker` (one worker thread, results posted to the UI thread, cancellable generations).
- [x] **P1-8 Device remote control over USB: frame dump and injected touch.** `core/remote` serial commands (`>shot`, `>tap`, `>long`, `>swipe`, `>log`, `>refresh`, ...), the same on the simulator with `--remote`; `tools/device.py`, `make device-shot`. The dump shows what the firmware drew, not what the panel shows.

## Phase 2: UI kit and app framework

Launcher with real tiles, apps open and close, Settings exists. Everything in the simulator first.

- [x] **P2-1 Widget base, gestures, layout, Label, IconView.** `ui/widget`, `core/gesture` (tap, long press, swipes), layout containers, dirty-widget redraw.
- [x] **P2-2 Button, IconButton, Toggle, ListItem, Tile, Badge, Pager.** Widgets with pressed states as Feedback pushes in pure black and white.
- [x] **P2-3 Sheet, Toast, EmptyState, Segmented, Stepper, LevelBar.** As in UI.md; a closed sheet redraws what was under it.
- [x] **P2-4 App framework.** `core/app` (`App`, `Screen`, `Nav`, `AppContext`), `core/shell`, `core/store`, `POS_REGISTER_APP`, status, title and tab bars.
- [x] **P2-5 Launcher, app template, new-app script.** `apps/launcher`, `apps/_template`, `tools/new-app.sh`.
- [x] **P2-6 Config service and Settings skeleton.** `core/config` (typed, persisted); Settings with Display, About and Developer.
- [x] **P2-7 Keyboard and TextBlock.** `ui/keyboard` (three layers, per-key feedback), text field, `ui/textblock` with pagination; script command `type`.

## Phase 3: Connectivity and power

- [x] **P3-1 Device power HAL.** Battery, charging, light sleep with wake reason, power-off with RTC wake; side-button findings in HARDWARE.md.
- [x] **P3-2 Net service and HTTP.** Wi-Fi STA with fast reconnect, HTTPS with a CA bundle, connect on demand and linger (`core/net`, `ctx.http`); libcurl and fixtures in the simulator.
- [x] **P3-3 Secrets from the SD card, serial commands.** `core/secrets` reads `/sd/paperos_config` (KEY=VALUE, D19); `>status` shows key names and lengths only.
- [x] **P3-4 PowerPolicy: idle sleep and auto-off.** `core/power_policy`: light sleep between inputs, auto-off, app state saved on stop, last app remembered.

- [~] **P3-5 Power measurements.**
  Built: the `/data/power.csv` logger (`core/power_log`), battery values on the Developer screen,
  `>cat` / `tools/device.py cat`. Left: the battery sessions and the CPU frequency experiment.
  Read: POWER.md (Measuring). Needs the device on battery for several hours.
  Do: run the three battery sessions from POWER.md, reading `/data/power.csv` afterwards; try
  80 MHz CPU while Wi-Fi is off and measure page-turn time and current.
  Done when: the estimates in POWER.md are replaced by measured numbers and STATUS.md is updated.

- [~] **P3-6 Sleep screen and fast resume.**
  Built: the sleep screen before power-off (`core/sleep_screen`), resume of the last app and the open
  book without Wi-Fi, goldens `sleep-*`. Left: timing on the device.
  Do: measure a side-button press to a usable screen on the device; look at the sleep screen on the
  panel with the camera right after power-off and hours later.
  Done when: button press to usable screen is under 2.5 s, measured and recorded in POWER.md.

- [x] **P3-7 Wi-Fi settings, time sync, status bar.** Settings > Wi-Fi (scan, join, forget), SNTP to the RTC with the configured time zone, status bar with time, Wi-Fi and battery.

## Phase 4: Reader

- [x] **P4-0 Push files to the SD card over USB.** `>put` / `>ls` / `>rm` limited to `/sd/books/` and `/sd/paperos/`; `tools/push.py`, `make books`.
- [x] **P4-1 TXT reading.** Page screen with touch zones, pagination through TextBlock, saved position, footer.
- [x] **P4-2 Library screen.** Scan of `/sd/books`, list with progress, recent-book card, empty state.
- [x] **P4-3 Zip and EPUB structure.** miniz in `lib/miniz`, `core/zip`, container / OPF / nav and NCX parsing; fixture EPUBs from `tests/fixtures/make-epub.py`.
- [x] **P4-4 XHTML to chapters, page index, indexing on the worker.** `apps/reader/xhtml`, chapter cache, page index files, "Preparing book..." progress, instant reopen from cache.
- [x] **P4-5 Reader menu.** Font size, contents, go to, clean screen; cover on the library card.
- [x] **P4-6 Reader on the device.** Page turn, ghosting and open times measured on the device (apps/reader.md, DISPLAY.md); a clean every tenth page.

## Phase 5: Home (Home Assistant)

- [x] **P5-1 HA client.** `core/ha` (`ctx.ha()`): get, post, service calls with responses, kept-alive connection, error mapping; Settings > Home Assistant "Test connection" (D18).
- [x] **P5-2 States and `home.json`.** Per-entity state reads (visible tab first), item view models, default `home.json` as an example house, cache.
- [x] **P5-3 Lights tab.** Rows with Toggle and a brightness sheet, optimistic updates with revert and toast.
- [x] **P5-4 Climate and More tabs, member sheet, scenes.** As in apps/home.md.

- [~] **P5-5 Polling, cache, device check.**
  Built: cache-first open, 30 s polling while the app is in use, offline handling
  (`home-offline.script`). Left: device timings and a run with a non-admin token.
  Read: apps/home.md, BACKEND.md, D18.
  Do: on the device, measure tap to visible change on a toggle, and app open with Wi-Fi off to fresh
  data (cold and warm); check whether polling needs a longer Wi-Fi linger. Run once with the token of a
  dedicated non-admin Home Assistant user.
  Done when: the timings in apps/home.md are met and recorded, and the non-admin run works (or the
  missing permission is documented).

## Phase 6: Inbox (WhatsApp, Mail)

Home Assistant is not trusted with message text (D22). The inbox comes from a companion Android app
(`android/`) that encrypts each phone notification for the device; Home Assistant only relays
ciphertext. The protocol is `docs/COMPANION.md`. Read BACKEND.md "Companion app" and D22 before any
task here.

- [-] **P6-1 WhatsApp add-on in Home Assistant.** Dropped: superseded by D22 (no message text in Home Assistant).
- [-] **P6-2 Proton Mail Bridge + IMAP integration.** Dropped: superseded by D22.
- [-] **P6-3 Install the inbox package.** Dropped with the add-on route (D22); only `script.paperos_inbox_clear` remains in `ha/paperos_inbox.yaml`.
- [x] **P6-4 Inbox app.** WhatsApp and Mail lists, grouping, chat and mail detail, cache, errors, "Not set up yet" for a missing feed (apps/inbox.md).
- [x] **P6-5 Badges and mark-all-read.** Seen marker, launcher badge, clear through the relay.
- [x] **P6-6 Text clean-up.** `core/text_clean`: HTML to text, entities, emoji, long words, links shortened to the host.

- [~] **P6-7 Clipboard and WhatsApp sending** (D21, D22).
  Built: `core/clipboard`, the text-field sheet (Paste / Copy all / Clear), compose and confirmation;
  a reply is an encrypted `reply` command for the phone, only to chats with a live notification
  (apps/inbox.md "Replying"). Left: a real reply end to end, which needs P6-11.
  Done when: a reply typed on the Paper arrives in the WhatsApp chat on the phone.

- [x] **P6-C0 Android app v0.** Notification listener for WhatsApp and Proton Mail in `android/`; it became the base of the P6-11 companion app.
- [x] **P6-8 Protocol and crypto.** `docs/COMPANION.md` (threat model, pairing, envelopes, replay rules), `core/companion_crypto` on libsodium, shared vectors in `tests/fixtures/companion/`.
- [x] **P6-9 Home Assistant relay.** Scripts and queue sensors in `ha/paperos_inbox.yaml` that hold ciphertext only, recorder-excluded (COMPANION.md section 6).
- [x] **P6-10 PaperOS decrypt side.** `core/companion`, `apps/inbox/relay_fetch`, pairing with QR code and fingerprint in Settings > Home Assistant > Phone, inbox and scheduled refresh on the relay, replies as `reply` envelopes.

- [~] **P6-11 Android companion app.**
  Built (`android/`, see its README): notification listener for WhatsApp (MessagingStyle) and Proton
  Mail, phone key in the Android Keystore, QR pairing, envelopes tested against the shared vectors,
  `RelayService` foreground service with outbox and back-off, removals as `read` / `dismissed`, replies
  through the notification's RemoteInput behind the send guards (off until "Allow replies from the
  Paper" is switched on). Left: the end-to-end acceptance run.
  Read: COMPANION.md, D22, `android/README.md`.
  Do: with a paired phone and device, run real traffic through the live relay. Notification contents
  are private: check with numbers and counts only, never log or commit them.
  Done when: a real WhatsApp message and a real Proton Mail notification show on the device within
  seconds; Home Assistant holds only ciphertext (checked in its state and in
  `.storage/core.restore_state`); one reply sent from the Paper arrives in the chat.

- [~] **P6-12 Clipboard sync with the phone** (D23).
  Built: `clip` in both directions (COMPANION.md section 7). Paper: `clipboard::Sync`
  (`apps/inbox/clipboard_sync`) pushes every copy, resends after an offline failure, and reads the
  relay before a paste; switch in Settings. Phone: share target "Send to Paper", Quick Settings tile,
  app button and switch, `setPrimaryClip` on a received `clip`. Tested in the simulator and the JVM
  tests. Left: the end-to-end check.
  Done when: on a paired phone and device, a copy on the Paper is on the phone's clipboard within a
  minute, and text shared on the phone is offered by Paste on the Paper.

## Phase 7: Polish

- [~] **P7-1 Display lab and tuning.**
  Built: Developer > Display lab (gray ramp, ghost test, text samples, counters), running on the
  device; Feedback and Content pushed per band of changed rows (DISPLAY.md rule 7). Left: tuning, the
  sub-rectangle push and the custom waveform.
  Read: DISPLAY.md, D6.
  Do: tune `ghostLimit` and the settle delay on the device with the camera; push only the changed
  sub-rectangle for small rects (`pushImage` from the canvas buffer, DISPLAY.md "Device implementation
  notes"); optionally try a custom Text waveform.
  Done when: defaults are chosen from camera measurements and recorded in DISPLAY.md; small pushes are
  measurably cheaper; the custom waveform is adopted or rejected with numbers.

- [x] **P7-2 UI scripts on the device.** `tools/device.py script` / `scripts` run UI scripts against the device and compare frame dumps with the goldens (SIMULATOR.md).
- [x] **P7-3 Over-the-air update.** `make ota`: a signed push while the update page is open, second app slot, bootloader rollback (D37).
- [x] **P7-4 Book upload over Wi-Fi.** Settings > Storage > Files over Wi-Fi: WebDAV and a browser upload page behind a one-time code (D30).
- [x] **P7-5 Scheduled refresh.** RTC wake every N hours to read the inbox and redraw the sleep screen; off by default (POWER.md).
- [x] **P7-6 Quick panel.** Swipe down from the status bar: Wi-Fi state, battery, sleep now, clean screen.

- [~] **P7-7 Camera on the panel.**
  Built: `tools/cam.py` (`shot`, `video`, `compare`, `around`, `lock`), `make cam-shot` / `cam-video` /
  `cam-compare` / `cam-target`; used for ghosting, the Clean flash, the gray ramp and reader ghosting.
  Left: the camera items in STATUS.md "Pending device checks".
  Read: DISPLAY.md "Camera on the panel".
  Note: the set-up is not stable over days. Register the panel on every run, compare numbers only
  within one session, never use camera images as goldens.
  Done when: every camera item in STATUS.md "Pending device checks" has had a first pass.

- [~] **P7-8 Somdoku.**
  Built: port of the Somdoku web game's generator and game logic, checked against 500 days of
  vectors; ready / play / done views; score to the clipboard (apps/somdoku.md). Tested in the
  simulator; on the device only the ready screen was seen. Left: a full device run.
  Done when: a puzzle is played and solved on the device, the generation time on the worker is
  recorded, and the board and hatched squares are checked on the panel with the camera.

- [~] **P7-9 Word game.**
  Built: a daily five-letter word game of its own (own name, word lists and day order, D26): game
  logic with unit tests, game and statistics screens, weekly word-list fetch, score to the clipboard
  (apps/wordgame.md). Played and solved on the device. Left: the lost screen on the device and the
  contrast of used-up keys (gray(10)) on the panel.
  Done when: both are checked with frame dumps and the camera, and the key colour is adjusted if the
  used-up keys are too faint.

- [~] **P7-10 Agenda** (D28).
  Built: phone side (`AgendaSync`: visible calendars, 14 days as `agenda` runs, changed days and a
  daily full window, switch "Share the agenda" off by default); Paper side (`core/agenda`, the Agenda
  app with Day and List views, the next event on the launcher and the sleep screen). Tested in the
  simulator, host unit tests and the Android JVM tests. Left: the run with a real phone and device.
  Read: D28, COMPANION.md 7.1 `agenda`, apps/agenda.md.
  Done when: an event made on the phone shows on the Paper within two minutes of a relay read, and an
  edit and a delete follow; the day view's blocks are checked on the panel with the camera.

- [x] **P7-11 Weather.** Weather app from a Home Assistant weather entity and `weather.get_forecasts`: now, today and tomorrow in three-hour blocks, ten days (apps/weather.md, D29).
- [x] **P7-12 Teletekst.** NOS Teletekst from its JSON as the 40 x 24 grid, links, subpages, number pad, saved pages (apps/teletekst.md, D32).
- [x] **P7-13 Idle return to the launcher.** Built (D33), then replaced by P7-19 (D38).
- [x] **P7-14 Hacker News.** Top / New / Ask / Show lists, comment threads as pages, articles as text through a reader service, QR code for the link (apps/hackernews.md, D34).
- [x] **P7-15 Inter drawn as wide as it is measured.** M5GFX patch 5 in `tools/patch_m5gfx.py` rounds fractional advances as the metrics do; unit test for every baked glyph (D24).
- [x] **P7-16 Today board on the sleep screen.** The sleep screen shows the date, weather, next agenda events, the open book and unread counts; switch `power.sleepBoard` (POWER.md "Sleep screen").
- [-] **P7-17 Home Assistant states on the Today board.** Dropped; not built.
- [x] **P7-18 Read later.** Links shared from the phone are queued (`core/readlater`), read through the reader service and saved as `.md` books under `/sd/books/Articles/`.

- [~] **P7-19 When unused: the sleep screen, staying on** (D38).
  Built: one idle time (`power.autoOffMin`) and one choice (`power.idleShow`: Home or the sleep
  screen, default the sleep screen), replacing the idle return of P7-13. On USB the sleep screen stays
  on with "Tap to start", redraws when it changes and reads the network every `refreshHours`; an idle
  power-off resumes only a book or a game; a tap on a part of the board opens that app at that place
  (apps/launcher.md "When unused"). Run on the device on USB, including a tap on an agenda row.
  Left: device checks.
  Read: D38, apps/launcher.md "When unused", POWER.md.
  Done when: on the device, the battery path (idle power-off, resume of a book or game only), a tap on
  the weather and on an unread count, and "Then show: Home" work; the board is checked on the panel
  with the camera.

- [x] **P7-20 Wireless development.** The remote commands over Wi-Fi on port 8266 after a signed sign-in, updates as `>ota`, `--wifi` for `device.py` / `view.py` / `ota.py`; Developer as a menu (D39).
- [x] **P7-21 Remote control from the companion app.** The app's Maintenance section: Remote control (the Paper's screen and touch over Wi-Fi) and Firmware update (`make ota-phone`) (D40, COMPANION.md section 10).
- [-] **P7-22 Pixelpuzzel.** Dropped: a daily nonogram, built and then removed.
- [x] **P7-23 Chess.** Tap-to-move board against the Lichess computer (Board API, `LICHESS_TOKEN`) or an offline engine on the Paper; the game kept in the store (D41, apps/chess.md).
- [x] **P7-24 Sonos in the Home app.** Music tab for a `media` item in `home.json`: now playing with cover, transport, volume, mute, shuffle, repeat and stations (apps/home.md "Music tab").

## Phase 8: Optional

- [-] **P8-1 Notification-sensor feeds.** Dropped: D8 is no longer a fallback since D22 (it puts message text in Home Assistant).

- [ ] **P8-2 Aggregate Home sensor.**
  A single template sensor in Home Assistant that carries the states the Home app needs, so a tab
  needs one read instead of one per entity. Only worth doing if per-entity reads are too slow.
  Read: D18, BACKEND.md, apps/home.md.
  Do: measure the Lights tab open time on the device first; above 3 s, add the sensor (in
  `ha/paperos_inbox.yaml` style, recorder-excluded) and read it from `core/ha`, with per-entity reads
  as the fallback.
  Done when: the tab opens under 3 s on the device, or the measurement shows the sensor is not needed
  (record it in STATUS.md and drop the task).

---

## Requirement coverage

Where each product requirement is met.

| Requirement | Where it is met |
|---|---|
| Wi-Fi | P3-2, P3-7 |
| Battery friendly | POWER.md; P3-1, P3-4, P3-5, P3-6, P7-5 |
| Optimal rendering | DISPLAY.md; P1-3, P1-4, P4-6, P7-1, P7-7 |
| Consistent modern UI with icons | UI.md; P1-6, P2-1 to P2-7 |
| Fast | Fast-then-settle (D6), cache-first apps, fast resume (P3-6) |
| Fits the device | Budgets in ARCHITECTURE.md; firmware size rules (D31) |
| Testable with and without a device | SIMULATOR.md; P1-4, P1-5, P1-8, P7-2, P7-7, P7-20 |
| Updates without USB | P7-3, P7-20, P7-21 |
| E-books | Phase 4; P4-0 and P7-4 to get books onto the card; P7-18 for articles |
| Easy to add apps | APPS.md; P2-4, P2-5 |
| WhatsApp and mail, unread | P6-4 to P6-11 (companion app, encrypted relay through Home Assistant, D22) |
| Replies and clipboard | P6-7 (D21), replies through the phone P6-10 and P6-11 (D22), clipboard sync P6-12 (D23) |
| Home Assistant control | Phase 5; P7-24 |
| Agenda | P7-10 (the phone's calendar, one way through the relay, D28) |
| Weather | P7-11 |
| Teletekst | P7-12 |
| Hacker News | P7-14 |
| Games | P7-8, P7-9, P7-23 |
