# Roadmap

Work proceeds task by task, top to bottom. `STATUS.md` says which task is next and records what
happened. Each task is sized for one working session.

## How to work a task

1. Read `STATUS.md`, then the task card, then every doc under **Read**.
2. Implement only what the card says. If something outside the card looks necessary, note it in
   `STATUS.md` under "Notes for later tasks" instead of doing it.
3. Run the card's **Done when** checks and `make check`. For UI work, look at the screenshots.
4. **Device check**: run it if a device is attached (`ls /dev/cu.usbmodem*`). If not, add it to
   "Pending device checks" in `STATUS.md`. Never report a device check as passed without running it.
5. Tick the box here, update `STATUS.md` (state, next task, measurements, surprises), update any
   doc the work made wrong, commit with message `<task id>: <summary>`.
6. If a card's assumption turns out false (API missing, measurement far off), stop, write what
   was found in `STATUS.md`, and propose a change; do not improvise a different architecture.

Legend: `[x]` done, `[ ]` open, `[~]` partly done (see STATUS.md).

---

## Phase 0: Foundation  (done 2026-11-15)

- [x] **P0-1 Toolchain and skeleton.** PlatformIO, `sim` + `papers3` environments, partitions,
  Makefile, shared `core/demo` drawn by both platforms.
- [x] **P0-2 Headless simulator.** `program --headless --shot` writes a 16-level PNG.
- [x] **P0-3 First flash and measurements.** Factory image backed up, spike flashed, facts and
  refresh timings recorded in `STATUS.md`, `HARDWARE.md`, `DISPLAY.md`.

## Phase 1: Platform core

Goal: the real skeleton replaces the spike. Shared code runs an event loop, draws through the
DisplayManager, and the simulator can run scripts and compare screenshots.

- [x] **P1-1 HAL interfaces, logging, file paths.**
  Read: ARCHITECTURE.md (Layers, HAL interfaces, Storage layout).
  Do: create `src/hal/{display,input,clock,net,power,platform}.h` as specified;
  `src/core/log.h` (`POS_LOG(tag, fmt, ...)`: printf to stderr in sim, `Serial` on device via a
  platform hook `pos::hal::logWrite(const char*)`); `src/core/fs.{h,cpp}` (`resolve()`,
  `readFile`, `writeFile` atomic via temp + rename, `exists`, `listDir`, `mkdirs`).
  Add `src/core/os.{h,cpp}` with `pos::Os(hal::Platform&)`, `setup()`, `loopOnce()`; move the
  demo drawing behind it. Both `main.cpp` files construct their Platform and call it (HAL
  implementations may be stubs that keep today's behaviour).
  Done when: `make check` passes; `make shot` output is pixel-identical to before; no platform
  header is included outside `src/platform/` (`grep -rE "Arduino.h|M5Unified.h|SDL.h" src/core src/ui src/apps src/hal` prints nothing).

- [x] **P1-2 Device HAL: display, touch, clock, storage.**
  Read: HARDWARE.md, DISPLAY.md (Device implementation notes).
  Do: `src/platform/device/{display,input,clock,storage}.cpp`. Display = the `pushRect` code
  from the spike. Input: `M5.update()`, translate `M5.Touch.getDetail()` to Down/Move/Up
  events. Clock: `millis()`, `M5.Rtc` for wall time. Storage: mount SD at `/sd` (SPI pins from
  HARDWARE.md, 25 MHz) and LittleFS at `/littlefs` (format on first boot); log sizes.
  Done when: `make fw` passes; on the device the demo shows, taps log coordinates, the boot log
  prints SD and LittleFS sizes, a file written to `/data/test.txt` survives a reboot.
  Device check: all of the above. A 64 GB card is in the device and mounts with the spike's
  code (HARDWARE.md); without a card the boot log must say "no SD card" and continue.

- [x] **P1-3 DisplayManager.**
  Read: DISPLAY.md (all).
  Do: `src/core/display_manager.{h,cpp}`: `invalidate(Rect, Hint)`, `commit(canvas)`,
  `tick(nowMs)` (settle and clean timers), `nextDeadlineMs()`, `forceClean()`. Shadow buffer
  4-bit; diff shrinks rects to changed pixels (row/column scan); hint to mode table; ghost
  budget; parameters from a struct (config comes later).
  Tests (`tests/unit/display_manager_test.cpp`, with a fake `hal::Display` that records pushes):
  unchanged canvas pushes nothing; a 10x10 change inside a full-screen invalidation pushes
  about 10x10; Feedback pushes Fast immediately; Content pushes Fast then Text after the settle
  delay; input before the delay restarts it; budget triggers Quality on the next Screen hint;
  Quality resets the score.
  Done when: unit tests pass (`pio test -e sim`); the demo screen goes through the manager.

- [x] **P1-4 Simulator HAL: e-ink emulation, refresh log, input, clock.**
  Read: SIMULATOR.md (Target), DISPLAY.md (Simulator emulation).
  Do: `src/platform/sim/{display,input,clock}.cpp`: `panel` buffer with the three mode
  behaviours and residue; window shows `panel`; refresh log to `out/refresh.log` and memory;
  mouse to touch events (drag = Move); keys S, G, L, Q; fake clock in headless mode.
  Done when: in the window, tapping the demo shows a thresholded Fast pass then the settled
  gray after 600 ms (visible on the gray ramp); `out/refresh.log` lists the pushes.

- [x] **P1-5 Script runner, golden tests, unit test harness.**
  Read: SIMULATOR.md (UI scripts, Golden screenshots, Unit tests).
  Do: `src/platform/sim/script.cpp` (commands: wait, shot, tap, swipe, long, back,
  expect-no-quality, expect-max-pushed; battery/wifi/type can come with their features);
  CLI flags `--script --out --data --time --app`; `tools/ui-test.sh`; Makefile targets `test`,
  `ui-test`, `ui-accept`, `gallery` (writes `out/gallery.html`: every UI test screenshot with
  its name, for the user to browse), and `check` = sim + test + ui-test + fw; doctest header in
  `tests/unit/`; first script `tests/ui/demo.script` with golden.
  Done when: `make check` runs all four stages and passes; changing a pixel in the demo makes
  `make ui-test` fail and name the image.

- [x] **P1-6 Fonts and icons pipeline.**
  Read: UI.md (Type, Icons), DECISIONS.md D12.
  Do: download Inter, Literata (Google Fonts, OFL) and the Lucide icon font + its
  name-to-codepoint JSON into `assets/` with licence files; `assets/icons.txt`;
  `tools/gen-assets.sh` runs `npx lv_font_conv` (pin its version) for every row of the type
  table (bpp 4, `--format bin` output embedded as C arrays, or `--format lvgl` if M5GFX needs
  that: check `M5GFX/examples/Basic/LvglFont` and `lgfx_fonts.hpp` for which loader applies)
  and writes `src/gen/fonts.{h,cpp}`, `src/gen/icons.h`. `src/ui/theme.h` (colour and spacing
  tokens), `src/ui/text.{h,cpp}` (`ui::font(Type)`, `drawText`, `measureText`, UTF-8 aware
  ellipsis), `ui::icon(...)`. Commit generated files (so builds need no Node).
  Done when: the demo screen uses Inter and real Lucide icons in the tiles; `make shot` looks
  right (letters sharp, icons centred); firmware size reported and under 3 MB.
  Device check: text and icons look crisp on the panel after a Quality refresh.

- [ ] **P1-7 Event loop, timers, worker.**
  Read: ARCHITECTURE.md (Runtime model).
  Do: `src/core/loop.{h,cpp}` (the five steps; idle calls a stub PowerPolicy that sleeps 10 ms
  on device / waits for SDL events in sim), `src/core/timers.{h,cpp}`, `src/core/worker.{h,cpp}`
  (`std::thread`, job queue, results posted to the UI thread; on device set the pthread stack
  to 16 KB with `esp_pthread_set_cfg` inside `platform/device`).
  Tests: timers fire in order; a worker job result arrives on the UI thread; a cancelled
  generation drops the callback.
  Done when: unit tests pass; demo: tapping a tile starts a 1 s fake job on the worker and the
  tile label changes when it completes, in sim and on device.

- [x] **P1-8 Device remote control over USB: frame dump and injected touch.**
  Why: the user does not want to look at or touch the device during development. This makes
  device checks scriptable. Do it right after P1-2 (it needs only the device HAL).
  Do: a serial line reader in `platform/device` (lines starting with `>`), dispatching to
  shared handlers in `src/core/remote.{h,cpp}` so the simulator can accept the same commands
  on stdin with `--remote`:
  `>shot` replies `#shot 540 960 <bytes> <crc32>` + newline, then the current frame as raw
  4-bit packed pixels (259200 bytes = 540*960/2; high nibble first, 0 black..15 white) taken from the canvas (after P1-3: from the
  DisplayManager's shadow, i.e. what was pushed to the panel);
  `>tap x y`, `>long x y`, `>swipe x1 y1 x2 y2` inject touch events into the input queue
  exactly as real touches; `>log` prints the last 50 log lines; `>refresh` prints the refresh
  log tail (after P1-3); `>reboot`.
  `tools/device.py` (pyserial from `~/.platformio/penv/bin/python`): `shot out/device.png`
  (verifies the CRC, writes a PNG), `tap X Y`, `swipe ...`, `log`, `wait-boot`. Makefile:
  `make device-shot`.
  The simulator accepts the same commands (`program --headless --remote`, replies on stdout;
  `--name M5PaperS3` makes its footer match a device dump).
  Tests: handler parsing in unit tests (from P1-5 on); round trip against the simulator.
  Done when: `make device-shot` writes `out/device.png` from the real device and it is
  pixel-identical to `make shot` for the same screen; `tools/device.py tap 140 300` draws the
  touch marker on the device (visible in the next `device-shot`).
  Limits to state in reports: the dump shows what the firmware drew, not what the panel
  physically shows (ghosting, incomplete waveforms).

## Phase 2: UI kit and app framework

Goal: launcher with real tiles, apps open and close, settings exists. Everything in the
simulator first; each task ends with screenshots.

- [ ] **P2-1 Widget base, gestures, layout, Label, IconView.**
  Read: UI.md (Foundations, Widgets, Gestures).
  Do: `src/ui/widget.{h,cpp}`, `src/core/gesture.{h,cpp}`, `Column/Row/Stack/Spacer/Padding`,
  `Label` (wrapping, maxLines, ellipsis), `IconView`. Root widget tree drawn by the loop;
  invalidation marks rects in the DisplayManager; only dirty widgets redraw (clear own frame to
  paper, draw).
  Tests: gesture recogniser (tap, long, 4 swipes, cancel when moved); Column layout with flex.
  Done when: a test screen (`tests/ui/widgets1.script`) shows labels in all type tokens and
  icons at both sizes; golden accepted.

- [ ] **P2-2 Button, IconButton, Toggle, ListItem, Tile, Badge, Pager.**
  Read: UI.md (Widgets, Patterns), DISPLAY.md (Rules for UI code).
  Do: the widgets with pressed states (Feedback hint, pure black/white).
  Done when: `tests/ui/widgets2.script` shows each widget in normal/pressed/on states; a tap on
  a Toggle passes `expect-max-pushed 12000`; a Pager changes pages of a 20-row list.

- [ ] **P2-3 Sheet, Toast, EmptyState, Segmented, Stepper, LevelBar.**
  Do: as in UI.md. Sheet restores what was under it when closed (redraw parent).
  Done when: `tests/ui/widgets3.script` goldens accepted.

- [ ] **P2-4 App framework.**
  Read: APPS.md (all).
  Do: `src/core/app.{h,cpp}`: `App`, `Screen`, `Nav`, `AppContext` (nav, async, timer, toast,
  clock, log; `store` and `cache` on top of core/fs with ArduinoJson: add the library here and
  record it in ARCHITECTURE.md), registry + `POS_REGISTER_APP`, linker flag so registrations
  are kept; `src/ui/{status_bar,title_bar,tab_bar}.cpp`.
  Done when: the `hello` app from APPS.md compiles unchanged, opens from a temporary launcher
  list, counts taps, and back returns; unit test for store round-trip.

- [ ] **P2-5 Launcher, app template, new-app script.**
  Read: apps/launcher.md.
  Do: `src/apps/launcher/`, `src/apps/_template/` (the hello app, excluded from the build),
  `tools/new-app.sh`. Placeholder apps registered for Read, Home, WhatsApp, Mail (an
  `EmptyState` "Coming soon") so the grid is real.
  Done when: `tests/ui/launcher.script` golden shows 5 tiles with icons; opening and closing an
  app passes `expect-no-quality`; `tools/new-app.sh demo "Demo" Info && make sim` builds and the
  tile appears (then delete the demo app).
  Device check: launcher on the panel; tap-to-open feels immediate (Fast pass within ~0.3 s).

- [ ] **P2-6 Config service and Settings skeleton.**
  Read: apps/settings.md.
  Do: `src/core/config.{h,cpp}` (typed getters, defaults, load/save); Settings app with the
  list, Display (clean now, ghost limit, settle delay: wired to the DisplayManager), About,
  Developer (heap, PSRAM, refresh counters, log tail).
  Done when: goldens for the screens; changing ghost limit persists across a simulator restart.

- [ ] **P2-7 Keyboard and TextBlock.**
  Do: `ui::Keyboard` (3 layers, key press feedback limited to the key rect), text field;
  `ui::TextBlock` (paragraphs with bold/italic runs, pagination API used later by reader and
  inbox). Script command `type`.
  Tests: TextBlock pagination properties (see apps/reader.md Tests).
  Done when: `tests/ui/keyboard.script` types a 12-character password; each key press passes
  `expect-max-pushed 6000`.

## Phase 3: Connectivity and power

- [ ] **P3-1 Device power HAL.**
  Read: POWER.md, HARDWARE.md (Power behaviour).
  Do: `platform/device/power.cpp`: battery (averaged), charging, `lightSleep(maxMs)` using
  `M5.Power.lightSleep(us, true)` after `M5.Display.waitDisplay()`, returning the wake reason;
  `powerOff(wakeAfterSec)` using `M5.Power.powerOff()` / `timerSleep`. Developer screen gets
  "Sleep test" (light sleep 10 s, report wake reason) and "Power off".
  Also find out: can a running program detect a side-button click? (try `M5.BtnPWR`, GPIO
  scan); record the answer in HARDWARE.md.
  Device check: touch wakes from light sleep and the waking tap is delivered; power off works;
  the button powers on; RTC timer power-on works.

- [ ] **P3-2 Net service and HTTP.**
  Read: POWER.md (Wi-Fi policy), ARCHITECTURE.md (HAL Net), SIMULATOR.md (HTTP fixtures).
  Do: `platform/device/net.cpp` (WiFi STA, stored BSSID/channel fast connect, `HTTPClient` +
  `NetworkClientSecure` with CA bundle, body cap 512 KB, timeouts); `platform/sim/net.cpp`
  (libcurl, fixtures mode, `wifi off` script command); `src/core/net.{h,cpp}` (connect on
  demand, linger timer, `ctx.http`).
  Tests: fixture lookup; linger logic with fake clock.
  Done when: a Developer "HTTP test" fetches a URL (fixture in sim).
  Device check: HTTPS GET to a public site works; connect time and heap logged; Wi-Fi goes off
  after the linger time.

- [ ] **P3-3 Secrets from the SD card, serial commands.**
  Read: apps/settings.md (Secrets and provisioning), BACKEND.md (Connection), DECISIONS.md D19.
  Do: `src/core/secrets.{h,cpp}`: parse `/sd/paperos_config` (KEY=VALUE) at boot, `get(key)`,
  `has(key)`; never log values. Net service tries `WIFI_SSID`/`WIFI_PASSWORD` first. Add to the
  serial commands from P1-8: `>status` (key names + lengths, Wi-Fi, heap). Simulator reads `sim-data/sd/paperos_config`.
  Tests: parser (comments, blank lines, CRLF, `=` inside the value, missing file).
  Done when: unit tests pass; `>status` on the device lists `HA_TOKEN(183)` and no value; a
  grep of the firmware log for the first 8 characters of the token finds nothing (do the grep
  with a script that reads the token from the simulator copy without printing it).

- [ ] **P3-4 PowerPolicy: idle sleep and auto-off.**
  Read: POWER.md (Idle rules, Auto-off).
  Do: `src/core/power_policy.{h,cpp}` implementing the rules with `hal::Power`; app state
  save on `onStop`; last app remembered.
  Tests: policy decisions with fake clock/platform (sleeps only when all conditions hold;
  sleep length = next deadline; auto-off fires).
  Device check: device sleeps between taps (log line with sleep durations), UI still feels
  immediate; auto-off after the configured time.

- [ ] **P3-5 Power measurements.**
  Read: POWER.md (Measuring). Needs the device and several hours; mostly waiting.
  Do: `/data/power.csv` logger, the three sessions, CPU 80 MHz experiment.
  Done when: POWER.md estimates replaced by measurements; STATUS.md updated.

- [ ] **P3-6 Sleep screen and fast resume.**
  Do: sleep screen before power-off; boot path restores the last app without Wi-Fi.
  Device check: button press to usable screen under 2.5 s (measure, record).

- [ ] **P3-7 Wi-Fi settings, time sync, status bar.**
  Do: Settings > Wi-Fi (scan, pick, keyboard, connect, save, forget); SNTP to RTC with the
  timezone from config; status bar shows time, Wi-Fi state, battery (icons by level).
  Done when: goldens with fixture scan results. Device check: join the home network from the
  device UI; time correct after sync and after a power cycle.

## Phase 4: Reader

- [ ] **P4-0 Push files to the SD card over USB.** Depends on P3-3 (serial commands).
  Do: serial command `>put <path> <size> <crc32>` followed by raw bytes in 4 KB chunks with an
  ACK per chunk (writes to `<path>.part`, renames when the CRC matches; only under `/sd/books/`
  and `/sd/paperos/`, never `paperos_config`); `>ls <dir>`, `>rm <path>` with the same path
  limits; `tools/push.py <file>... [--to /sd/books/]`; `make books` = fetch + push all.
  Done when: all six books from `tools/fetch-books.sh` are on the card and `>ls /sd/books`
  shows matching sizes. Target speed at least 100 KB/s (2 MB of books under 30 s).
  Alternative for the user at any time: take the card out and copy files into `/books/`.

- [ ] **P4-1 TXT reading.** Read: apps/reader.md. Page screen with touch zones, pagination via
  TextBlock, progress save/restore, footer. Put a public-domain TXT in `sim-data/sd/books/`.
  Done when: `tests/ui/reader.script` turns 5 pages with `expect-no-quality`; position survives
  restart.
- [ ] **P4-2 Library screen.** Scan `/sd/books`, list with progress, recent-book card (no cover
  yet), empty state. Golden.
- [ ] **P4-3 Zip and EPUB structure.** Vendor miniz (tinfl) in `lib/miniz`, `core/zip`,
  container.xml / OPF / nav+ncx parsing to `meta.json`. Unit tests with a small fixture EPUB
  (build one with a script in `tests/unit/fixtures/`), including malformed files.
- [ ] **P4-4 XHTML to chapters, page index, indexing on the worker.** `apps/reader/xhtml`,
  chapter cache files, `pages-<key>.idx`, "Preparing book..." progress, opening from cache.
  Unit tests per reader.md. Add one Project Gutenberg EPUB to `sim-data/sd/books/`.
  Done when: the EPUB opens, chapters and bold/italic render, reopening is instant.
- [ ] **P4-5 Reader menu.** Font size, contents, go to, clean screen; cover on the library card
  (`Hint::Image`). Goldens.
- [ ] **P4-6 Reader on the device.** Device check and tuning: page turn time, ghosting over 30
  pages, cold open of a 1 MB EPUB, index time. Record numbers; adjust `ghostLimit`/settle
  defaults if needed.

## Phase 5: Home (Home Assistant)

- [ ] **P5-1 HA client.** Read: BACKEND.md, DECISIONS.md D18. `src/core/ha.{h,cpp}` (the inbox
  uses it too): `get(path)`, `post(path, json)`, `callService(domain, service, json,
  wantResponse)`, kept-alive connection, error mapping (no token, no Wi-Fi, timeout, 401,
  other), Settings > Home Assistant "Test connection". Fixtures for each case.
  Ask the owner (once): which Home Assistant user does the token on the card belong to? It
  should be a dedicated non-admin user; and what LAN URL to use (`HA_URL` on the card).
  Device check: "Test connection" succeeds from the device on the home Wi-Fi.
- [ ] **P5-2 States and `home.json`.** Per-entity state reads (visible tab first), response
  parser to item view models, default `home.json` (complete the Avond scene from the dashboard
  repo). Verify every entity id against the live instance from the simulator
  (`--fixtures none`, token in `sim-data/sd/paperos_config`: ask the owner to copy the file
  from the card or add one; never print it). Measure the time for the Lights tab on the device;
  above 3 s, add the aggregate sensor described in BACKEND.md. Unit tests.
- [ ] **P5-3 Lights tab.** Rows with Toggle and LevelBar, optimistic updates, service calls,
  failure revert + toast. Goldens with fixtures.
- [ ] **P5-4 Climate and More tabs, member sheet, scenes.** Goldens.
- [ ] **P5-5 Polling, cache, device check.** Foreground refresh policy, cache-first open.
  Device check: timings in apps/home.md.

## Phase 6: Inbox (WhatsApp, Mail)

P6-4 to P6-6 (the device app) need only the feed contract and can be done first. P6-1 to P6-3
change the user's Home Assistant: each needs the owner present, and each follows the
home-assistant repo's own rules. Read BACKEND.md fully before any of them.

- [ ] **P6-1 WhatsApp add-on in Home Assistant.** Needs the owner (QR scan with the phone).
  Before installing, repeat the terms-of-service warning from BACKEND.md and get a clear yes.
  Do: add the add-on repository, install and start the WhatsApp add-on, link the account,
  install the `whatsapp` integration. Checks: (1) phone still gets WhatsApp notifications
  while the add-on is connected; (2) capture `whatsapp_message_received` for a direct and a
  group message and `whatsapp_message_sent` for a reply (Developer tools > Events); save
  redacted samples in `tests/fixtures/ha-events/`; (3) auto-responder, bot commands and
  mark-as-read are off. Record what was installed in the home-assistant repo's HANDOVER.md.
  Done when: the three checks are written down in STATUS.md with their results.
- [ ] **P6-2 Proton Mail Bridge + IMAP integration.** Needs the owner (Proton login, 2FA).
  Do: find how to run Bridge for Home Assistant (options a/b/c in BACKEND.md; say what will be
  installed where before doing it), configure the IMAP integration, enable message text,
  confirm an `imap_content` event for a test mail, try `imap.fetch` with `?return_response`
  over REST with the device token and save a redacted fixture.
  Done when: event and fetch samples are saved; setup recorded in HANDOVER.md.
- [ ] **P6-3 Install the inbox package.** Fix `ha/paperos_inbox.yaml` against the real samples
  (group names, entity ids), test both templates in Developer tools > Template, install into
  the home-assistant repo per its rules, verify: a new WhatsApp message and a new mail appear
  in the sensors within seconds; answering the chat removes its items; reading all mail empties
  the mail list; `script.paperos_inbox_clear` works with the device token.
- [ ] **P6-4 Inbox app.** Read: apps/inbox.md. List, grouping, chat detail, mail detail with
  full-text fetch, cache, errors, link-down notice, both registrations. Goldens with fixtures.
- [ ] **P6-5 Badges and mark-all-read.** Seen marker, launcher badge, clear via the script.
- [ ] **P6-6 Text clean-up.** `core/text_clean`: HTML to text, entities, emoji, long words,
  links shortened to the host name. Unit tests.

## Phase 7: Polish

- [ ] **P7-1 Display lab and tuning.** Developer > Display lab; tune defaults on the device;
  sub-rectangle push optimisation; optional custom Text waveform (DECISIONS.md D6).
- [ ] **P7-2 UI scripts on the device.** `tools/device.py script tests/ui/<name>.script` runs
  the simulator's script commands against the device through the P1-8 commands and compares
  the frame dumps with the goldens (pixel-identical expected: same code, same fonts).
- [ ] **P7-3 Over-the-air update.** `make ota` uploads to the device over Wi-Fi (ArduinoOTA or
  an HTTP upload endpoint), using the second app slot; rollback if the new image does not boot.
- [ ] **P7-4 Book upload over Wi-Fi.** Reader menu "Add books": device shows its address, a
  small web page accepts uploads into `/sd/books`.
- [ ] **P7-5 Scheduled refresh.** RTC wake every N hours to fetch inbox feeds and redraw the
  sleep screen with a summary; off by default; measure the cost.
- [ ] **P7-6 Quick panel** (swipe down): Wi-Fi state, sleep now, clean screen.

## Phase 8: Optional, ask the owner first

- [ ] **P8-1 Notification-sensor feeds** (DECISIONS.md D8) as a replacement if the WhatsApp
  add-on proves unreliable or risky: only the package triggers change.
- [ ] **P8-2 Aggregate Home sensor** in Home Assistant if per-entity reads are too slow (D18).

---

## Requirement coverage

| Requirement | Where it is met |
|---|---|
| Wi-Fi | P3-2, P3-7 |
| Battery friendly | POWER.md; P3-1, P3-4, P3-5, P3-6 |
| Optimal rendering | DISPLAY.md; P1-3, P1-4, P4-6, P7-1 |
| Consistent modern UI with icons | UI.md; P1-6, P2-1..P2-7 |
| Fast | Fast-then-settle (D6), cache-first apps, fast resume (P3-6) |
| Fits the device | budgets in ARCHITECTURE.md (firmware < 3 MB of 5 MB slot) |
| Testable with and without device | SIMULATOR.md; P1-4, P1-5, P1-8 (device frame dump + injected touch), P7-2 |
| E-books | P4; open books fetched by `tools/fetch-books.sh`, pushed to the card in P4-0 |
| Easy to add apps | APPS.md; P2-4, P2-5 |
| WhatsApp unread | P6 (Home Assistant WhatsApp add-on, D16) |
| Recent mail | P6 (Proton via Bridge + IMAP integration, full text on demand, D17) |
| Home Assistant control | P5 |
