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

Legend: `[x]` done, `[ ]` open, `[~]` partly done (see STATUS.md), `[-]` dropped (superseded by a decision).

---

## Phase 0: Foundation  (done 2026-11-15)

- [x] **P0-1 Toolchain and skeleton.** PlatformIO, `sim` + `papers3` environments, partitions,
  Makefile, shared `core/demo` drawn by both platforms.
- [x] **P0-2 Headless simulator.** `program --headless --shot` writes a 16-level PNG.
- [x] **P0-3 First flash and measurements.** Factory image backed up, spike flashed, facts and
  refresh timings recorded in `STATUS.md`, `HARDWARE.md`, `DISPLAY.md`.

## Phase 1: Platform core  (done 2026-11-15)

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

- [x] **P1-7 Event loop, timers, worker.**
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

- [x] **P2-1 Widget base, gestures, layout, Label, IconView.**
  Read: UI.md (Foundations, Widgets, Gestures).
  Do: `src/ui/widget.{h,cpp}`, `src/core/gesture.{h,cpp}`, `Column/Row/Stack/Spacer/Padding`,
  `Label` (wrapping, maxLines, ellipsis), `IconView`. Root widget tree drawn by the loop;
  invalidation marks rects in the DisplayManager; only dirty widgets redraw (clear own frame to
  paper, draw).
  Tests: gesture recogniser (tap, long, 4 swipes, cancel when moved); Column layout with flex.
  Done when: a test screen (`tests/ui/widgets1.script`) shows labels in all type tokens and
  icons at both sizes; golden accepted.

- [x] **P2-2 Button, IconButton, Toggle, ListItem, Tile, Badge, Pager.**
  Read: UI.md (Widgets, Patterns), DISPLAY.md (Rules for UI code).
  Do: the widgets with pressed states (Feedback hint, pure black/white).
  Done when: `tests/ui/widgets2.script` shows each widget in normal/pressed/on states; a tap on
  a Toggle passes `expect-max-pushed 12000`; a Pager changes pages of a 20-row list.

- [x] **P2-3 Sheet, Toast, EmptyState, Segmented, Stepper, LevelBar.**
  Do: as in UI.md. Sheet restores what was under it when closed (redraw parent).
  Done when: `tests/ui/widgets3.script` goldens accepted.

- [x] **P2-4 App framework.**
  Read: APPS.md (all).
  Do: `src/core/app.{h,cpp}`: `App`, `Screen`, `Nav`, `AppContext` (nav, async, timer, toast,
  clock, log; `store` and `cache` on top of core/fs with ArduinoJson: add the library here and
  record it in ARCHITECTURE.md), registry + `POS_REGISTER_APP`, linker flag so registrations
  are kept; `src/ui/{status_bar,title_bar,tab_bar}.cpp`.
  Done when: the `hello` app from APPS.md compiles unchanged, opens from a temporary launcher
  list, counts taps, and back returns; unit test for store round-trip.

- [x] **P2-5 Launcher, app template, new-app script.**
  Read: apps/launcher.md.
  Do: `src/apps/launcher/`, `src/apps/_template/` (the hello app, excluded from the build),
  `tools/new-app.sh`. Placeholder apps registered for Read, Home, WhatsApp, Mail (an
  `EmptyState` "Coming soon") so the grid is real.
  Done when: `tests/ui/launcher.script` golden shows 5 tiles with icons; opening and closing an
  app passes `expect-no-quality`; `tools/new-app.sh demo "Demo" Info && make sim` builds and the
  tile appears (then delete the demo app).
  Device check: launcher on the panel; tap-to-open feels immediate (Fast pass within ~0.3 s).

- [x] **P2-6 Config service and Settings skeleton.**
  Read: apps/settings.md.
  Do: `src/core/config.{h,cpp}` (typed getters, defaults, load/save); Settings app with the
  list, Display (clean now, ghost limit, settle delay: wired to the DisplayManager), About,
  Developer (heap, PSRAM, refresh counters, log tail).
  Done when: goldens for the screens; changing ghost limit persists across a simulator restart.

- [x] **P2-7 Keyboard and TextBlock.**
  Do: `ui::Keyboard` (3 layers, key press feedback limited to the key rect), text field;
  `ui::TextBlock` (paragraphs with bold/italic runs, pagination API used later by reader and
  inbox). Script command `type`.
  Tests: TextBlock pagination properties (see apps/reader.md Tests).
  Done when: `tests/ui/keyboard.script` types a 12-character password; each key press passes
  `expect-max-pushed 6000`.

## Phase 3: Connectivity and power

- [x] **P3-1 Device power HAL.**
  Read: POWER.md, HARDWARE.md (Power behaviour).
  Do: `platform/device/power.cpp`: battery (averaged), charging, `lightSleep(maxMs)` using
  `M5.Power.lightSleep(us, true)` after `M5.Display.waitDisplay()`, returning the wake reason;
  `powerOff(wakeAfterSec)` using `M5.Power.powerOff()` / `timerSleep`. Developer screen gets
  "Sleep test" (light sleep 10 s, report wake reason) and "Power off".
  Also find out: can a running program detect a side-button click? (try `M5.BtnPWR`, GPIO
  scan); record the answer in HARDWARE.md.
  Device check: touch wakes from light sleep and the waking tap is delivered; power off works;
  the button powers on; RTC timer power-on works.

- [x] **P3-2 Net service and HTTP.**
  Read: POWER.md (Wi-Fi policy), ARCHITECTURE.md (HAL Net), SIMULATOR.md (HTTP fixtures).
  Do: `platform/device/net.cpp` (WiFi STA, stored BSSID/channel fast connect, `HTTPClient` +
  `NetworkClientSecure` with CA bundle, body cap 512 KB, timeouts); `platform/sim/net.cpp`
  (libcurl, fixtures mode, `wifi off` script command); `src/core/net.{h,cpp}` (connect on
  demand, linger timer, `ctx.http`).
  Tests: fixture lookup; linger logic with fake clock.
  Done when: a Developer "HTTP test" fetches a URL (fixture in sim).
  Device check: HTTPS GET to a public site works; connect time and heap logged; Wi-Fi goes off
  after the linger time.

- [x] **P3-3 Secrets from the SD card, serial commands.**
  Read: apps/settings.md (Secrets and provisioning), BACKEND.md (Connection), DECISIONS.md D19.
  Do: `src/core/secrets.{h,cpp}`: parse `/sd/paperos_config` (KEY=VALUE) at boot, `get(key)`,
  `has(key)`; never log values. Net service tries `WIFI_SSID`/`WIFI_PASSWORD` first. Add to the
  serial commands from P1-8: `>status` (key names + lengths, Wi-Fi, heap). Simulator reads `sim-data/sd/paperos_config`.
  Tests: parser (comments, blank lines, CRLF, `=` inside the value, missing file).
  Done when: unit tests pass; `>status` on the device lists `HA_TOKEN(183)` and no value; a
  grep of the firmware log for the first 8 characters of the token finds nothing (do the grep
  with a script that reads the token from the simulator copy without printing it).

- [x] **P3-4 PowerPolicy: idle sleep and auto-off.**
  Read: POWER.md (Idle rules, Auto-off).
  Do: `src/core/power_policy.{h,cpp}` implementing the rules with `hal::Power`; app state
  save on `onStop`; last app remembered.
  Tests: policy decisions with fake clock/platform (sleeps only when all conditions hold;
  sleep length = next deadline; auto-off fires).
  Device check: device sleeps between taps (log line with sleep durations), UI still feels
  immediate; auto-off after the configured time.

- [~] **P3-5 Power measurements.** (prepared 2026-11-15: `/data/power.csv` logger, Developer values, `>cat`; the three battery sessions and the 80 MHz experiment are open)
  Read: POWER.md (Measuring). Needs the device and several hours; mostly waiting.
  Do: `/data/power.csv` logger, the three sessions, CPU 80 MHz experiment.
  Done when: POWER.md estimates replaced by measurements; STATUS.md updated.

- [~] **P3-6 Sleep screen and fast resume.** (simulator part done: sleep screen, resume of the last app and the open book, goldens `sleep-*`; device timing open)
  Do: sleep screen before power-off; boot path restores the last app without Wi-Fi.
  Device check: button press to usable screen under 2.5 s (measure, record).

- [x] **P3-7 Wi-Fi settings, time sync, status bar.**
  Do: Settings > Wi-Fi (scan, pick, keyboard, connect, save, forget); SNTP to RTC with the
  timezone from config; status bar shows time, Wi-Fi state, battery (icons by level).
  Done when: goldens with fixture scan results. Device check: join the home network from the
  device UI; time correct after sync and after a power cycle.

## Phase 4: Reader

- [x] **P4-0 Push files to the SD card over USB.** Depends on P3-3 (serial commands).
  Do: serial command `>put <path> <size> <crc32>` followed by raw bytes in 4 KB chunks with an
  ACK per chunk (writes to `<path>.part`, renames when the CRC matches; only under `/sd/books/`
  and `/sd/paperos/`, never `paperos_config`); `>ls <dir>`, `>rm <path>` with the same path
  limits; `tools/push.py <file>... [--to /sd/books/]`; `make books` = fetch + push all.
  Done when: all six books from `tools/fetch-books.sh` are on the card and `>ls /sd/books`
  shows matching sizes. Target speed at least 100 KB/s (2 MB of books under 30 s).
  Alternative for the user at any time: take the card out and copy files into `/books/`.

- [x] **P4-1 TXT reading.** Read: apps/reader.md. Page screen with touch zones, pagination via
  TextBlock, progress save/restore, footer. Put a public-domain TXT in `sim-data/sd/books/`.
  Done when: `tests/ui/reader.script` turns 5 pages with `expect-no-quality`; position survives
  restart.
- [x] **P4-2 Library screen.** Scan `/sd/books`, list with progress, recent-book card (no cover
  yet), empty state. Golden.
- [x] **P4-3 Zip and EPUB structure.** Vendor miniz (tinfl) in `lib/miniz`, `core/zip`,
  container.xml / OPF / nav+ncx parsing to `meta.json`. Unit tests with a small fixture EPUB
  (built by `tests/fixtures/make-epub.py` into `tests/fixtures/books/`, committed), including malformed files.
- [x] **P4-4 XHTML to chapters, page index, indexing on the worker.** `apps/reader/xhtml`,
  chapter cache files, `pages-<key>.idx`, "Preparing book..." progress, opening from cache.
  Unit tests per reader.md. Add one Project Gutenberg EPUB to `sim-data/sd/books/`.
  Done when: the EPUB opens, chapters and bold/italic render, reopening is instant.
- [x] **P4-5 Reader menu.** Font size, contents, go to, clean screen; cover on the library card
  (`Hint::Image`). Goldens.
- [x] **P4-6 Reader on the device.** (2026-11-16: page turn 109 ms to the driver; ghost flat at 1.1-1.3 % over 30 pages, defaults
  kept; cached open 30-230 ms, first open 10-16 s for the books on the card (the largest is 609 KB: no 1 MB EPUB was measured);
  three worker-order fixes; numbers in apps/reader.md and DISPLAY.md) Device check and tuning: page turn time, ghosting over 30
  pages, cold open of a 1 MB EPUB, index time. Record numbers; adjust `ghostLimit`/settle
  defaults if needed.

## Phase 5: Home (Home Assistant)

- [x] **P5-1 HA client.** (`core/ha`, `ctx.ha()`, unit tests per error; the owner's answers: token stays during development, non-admin token later, `HA_URL` on the card. Device re-check of "Test connection" with the new client is pending.) Read: BACKEND.md, DECISIONS.md D18. `src/core/ha.{h,cpp}` (the inbox
  uses it too): `get(path)`, `post(path, json)`, `callService(domain, service, json,
  wantResponse)`, kept-alive connection, error mapping (no token, no Wi-Fi, timeout, 401,
  other), Settings > Home Assistant "Test connection". Fixtures for each case.
  Ask the owner (once): which Home Assistant user does the token on the card belong to? It
  should be a dedicated non-admin user; and what LAN URL to use (`HA_URL` on the card).
  Device check: "Test connection" succeeds from the device on the home Wi-Fi.
- [x] **P5-2 States and `home.json`.** (model, parser, default file, cache and tests done in the simulator; live entity check and the Lights-tab timing are pending device checks: no card config in `sim-data`, device off) Per-entity state reads (visible tab first), response
  parser to item view models, default `home.json` (complete the Avond scene from the dashboard
  repo). Verify every entity id against the live instance from the simulator
  (`--fixtures none`, token in `sim-data/sd/paperos_config`: ask the owner to copy the file
  from the card or add one; never print it). Measure the time for the Lights tab on the device;
  above 3 s, add the aggregate sensor described in BACKEND.md. Unit tests.
- [x] **P5-3 Lights tab.** (simulator: `tests/ui/home.script`; the brightness LevelBar lives in the row's sheet, see apps/home.md) Rows with Toggle and LevelBar, optimistic updates, service calls,
  failure revert + toast. Goldens with fixtures.
- [x] **P5-4 Climate and More tabs, member sheet, scenes.** Goldens.
- [~] **P5-5 Polling, cache, device check.** (simulator part done: cache first, 30 s polling while used, `home-offline.script`; the device timings and the non-admin run are pending) Foreground refresh policy, cache-first open.
  Device check: timings in apps/home.md. One run with a non-admin token (a dedicated user, per the 2026-11-15 decision) before Phase 5 counts as done on the device, or list it under "Pending device checks" in STATUS.md.

## Phase 6: Inbox (WhatsApp, Mail)

**Changed on 2026-11-16 (DECISIONS.md D22):** Home Assistant is not trusted with message text. The inbox now comes from a
companion Android app that encrypts each phone notification for the device; Home Assistant only relays ciphertext. The
add-on route (P6-1 to P6-3, the send path of P6-7) is removed. The device app (P6-4 to P6-6) and the clipboard stay.
The owner said "Continue" without answering the companion-app questions; the defaults assumed are in STATUS.md (open question 6).
Read BACKEND.md "Companion app" and D22 before any of them. P6-C0 (a listener-only app) runs ahead of them. Order: P6-8 first; P6-9 and P6-10 can then run in parallel;
P6-11 last (it needs the relay and a device that can pair).

- [-] **P6-1 WhatsApp add-on in Home Assistant.** (done 2026-11-15, **removed 2026-11-16** by D22: session logged out, integration
  and add-on removed. History in STATUS.md "P6-1 progress" and git.)
- [-] **P6-2 Proton Mail Bridge + IMAP integration.** (parked 2026-11-16 with Bridge built but never logged in; **removed** by D22.)
- [-] **P6-3 Install the inbox package.** (WhatsApp half was live; **cut down 2026-11-16** to `script.paperos_inbox_clear` and a
  recorder exclusion, home-assistant commit `ebc133d`.)
- [x] **P6-4 Inbox app.** (simulator, from the contract and fixtures; see apps/inbox.md "As built") Read: apps/inbox.md. List, grouping, chat detail, mail detail with
  full-text fetch, cache, errors, link-down notice, both registrations. Goldens with fixtures. Since D22 a missing feed (404)
  shows "Not set up yet" and drops the saved list.
- [x] **P6-5 Badges and mark-all-read.** (simulator: seen marker, badge file for the launcher, `script.paperos_inbox_clear`) Seen marker, launcher badge, clear via the script.
- [x] **P6-6 Text clean-up.** (`core/text_clean`; the reader's XML tokenizer shares its entity decoder) `core/text_clean`: HTML to text, entities, emoji, long words,
  links shortened to the host name. Unit tests.
- [~] **P6-7 Clipboard and WhatsApp sending** (D21). The clipboard, the text-field sheet, compose and the confirmation are built and
  stay. **The send path changed** (D22): `script.paperos_whatsapp_send` was removed from Home Assistant; since P6-10 a send is an
  encrypted `reply` for the phone (only to chats with a live notification). Done when a real reply sent from the Paper arrives (P6-11).

- [~] **P6-C0 Android app v0** (P6-11 builds on it, in the same session) (started 2026-11-16, separately; source in `android/`): a notification
  listener for WhatsApp and Proton Mail only, no network, no Home Assistant, no crypto; installed on the Pixel 9 Pro (Android 17) over
  adb. It shows what the notifications really carry and becomes the base of P6-11. The four open questions still decide the network and
  crypto part.
- [x] **P6-8 Protocol and crypto.** (2026-11-16: `docs/COMPANION.md` v1 (commit 89aa3e3), `tests/fixtures/companion/vectors.json` made with
  pyca/cryptography, `core/companion_crypto` on libsodium (not Monocypher: one library on both platforms, D22 addendum), unit tests
  against the vectors and RFC 7748.) Read: D22, BACKEND.md "Companion app". Write `docs/COMPANION.md`: the threat model (Home
  Assistant reads, drops, delays and replays; never reads or forges), pairing (device X25519 key pair, private key in
  `/sd/paperos_config` under the secrets rules; QR code with the public key; the phone's public key back through Home Assistant;
  a short fingerprint on both screens that the user compares), the item envelope (version, sender key id, counter, nonce, ciphertext of
  the feed item JSON, size limits), the command envelope device to phone (reply with text, mark read, unpair), replay rules in both
  directions, key loss and re-pairing, what metadata Home Assistant still sees. Vendor Monocypher into `lib/` (platform-free, runs in the
  simulator and the firmware); `core/companion_crypto` with unit tests against known-answer vectors that the Android side will share
  (`tests/fixtures/companion/`). Done when: the document is reviewed with the owner, and seal/open round-trips, tampering, wrong keys
  and replays are unit-tested on the host.
- [x] **P6-9 Home Assistant relay.** (2026-11-16: home-assistant `e04d613`, scripts plus three queue sensors, no webhook: COMPANION.md 6;
  tested in Home Assistant's engine and live; **the recorder exclusion waits for a Home Assistant restart**.) Read: P6-8 output, the Home Assistant repo's rules. In `packages/paperos_inbox.yaml`: a
  webhook trigger (`local_only` false only if the phone must post from outside the home) into a trigger-based template sensor that keeps
  the last N blobs per feed (sizes capped, newest first, recorder-excluded), the clear path (`script.paperos_inbox_clear` drops blobs
  up to a counter), and the device-to-phone command queue the app reads. The webhook id is a secret: never committed, never logged. No
  plaintext, no send rule here. Validate YAML, push by that repo's rules, mirror to `ha/paperos_inbox.yaml`. Done when: posted test
  blobs appear in the sensor, the clear works with the device token (non-admin), nothing reaches the history database.
- [x] **P6-10 PaperOS decrypt side.** (2026-11-16: `core/companion`, `apps/inbox/relay_fetch`, Settings > Home Assistant > Phone with the QR
  code, the inbox and the scheduled refresh on the relay, replies and Somdoku's score as `reply` envelopes; goldens; on the device a full
  round trip through live Home Assistant with a scripted test phone: pairing code equal on both sides, three fake notifications shown,
  `paired` and `unpair` decrypted by the test phone. Not run on the device: a reply typed on the device.) Read: P6-8, apps/inbox.md. Pairing screen in Settings (generate keys, show the QR code and the
  fingerprint, store the phone's key), fetch the relay sensor, check sizes, decrypt, drop anything that fails authentication or
  replays, map to the feed contract and keep the existing list, detail and badge code. Mail detail without the full-text fetch. Reply
  in a chat becomes an encrypted command to the phone (no favourites target any more; Somdoku's "Stuur naar WhatsApp" moves to the
  clipboard or a reply, decided with the owner). Goldens with encrypted fixtures. Done when: the simulator shows a fixture feed decrypted
  end to end and rejects a tampered one; the same on the device against the live relay with test blobs.
- [~] **P6-12 Clipboard sync with the phone** (D23). `clip` in COMPANION.md 7 both ways; Paper:
  `clipboard::Sync` installed at boot (`apps/inbox/clipboard_sync`), push on every copy, pull before a paste, Settings switch; phone:
  share target, Quick Settings tile, app button and switch, `setPrimaryClip` on a received `clip`. Built and tested in the simulator
  and the JVM tests (2026-11-16). Done when: on the real phone and device, a copy on the Paper is on the phone's clipboard within a
  minute, and text shared on the phone is offered by Paste on the Paper.
- [ ] **P6-11 Android companion app.** Read: P6-8, the owner's answers (phone, Android version). Kotlin, sideloaded, signed with a key
  on the Mac (never in a repo). NotificationListenerService for WhatsApp and Proton Mail (MessagingStyle for WhatsApp), QR pairing,
  encryption with libsodium (Lazysodium) using the shared vectors, posting to the webhook, `onNotificationRemoved` as "read", the
  command queue (reply through the notification's RemoteInput action while it exists), **send guards on the phone** (length, 10 s gap,
  20 per hour, one chat), foreground service and the battery-optimisation exemption. Installed with adb on the user's phone with the
  user present. Done when: a real WhatsApp message and a real Proton Mail notification show on the device within seconds, Home
  Assistant holds only ciphertext (checked in its state and `.storage/core.restore_state`), and one reply the owner asked for arrives.

## Phase 7: Polish

- [~] **P7-1 Display lab and tuning.** (2026-11-15: the Display lab screen is built and runs on the device; tuning, the sub-rectangle push optimisation and a custom waveform are open and need a person's eyes) Developer > Display lab; tune defaults on the device;
  sub-rectangle push optimisation; optional custom Text waveform (DECISIONS.md D6).
- [x] **P7-2 UI scripts on the device.** (`tools/device.py script` / `scripts`, `>state`; which scripts run and compare: SIMULATOR.md) `tools/device.py script tests/ui/<name>.script` runs
  the simulator's script commands against the device through the P1-8 commands and compares
  the frame dumps with the goldens (pixel-identical expected: same code, same fonts).
- [ ] **P7-3 Over-the-air update.** `make ota` uploads to the device over Wi-Fi (ArduinoOTA or
  an HTTP upload endpoint), using the second app slot; rollback if the new image does not boot.
- [x] **P7-4 Book upload over Wi-Fi.** (2026-11-16; D30) Settings > Storage > Files over Wi-Fi: while
  the page is open the books folder is served as WebDAV (Finder, Android file managers) and as an upload page for a browser,
  behind a one-time code; on the device tested from the Mac (curl, a WebDAV client library); Finder and Android not tried
  (STATUS.md). A shortcut from the reader's library is left for later.
- [x] **P7-5 Scheduled refresh.** RTC wake every N hours to fetch inbox feeds and redraw the
  sleep screen with a summary; off by default; measure the cost. (2026-11-15: simulator and device with RTC wakes; awake time
  measured, the mAh figure in POWER.md is an estimate: no current meter)
- [x] **P7-6 Quick panel** (swipe down): Wi-Fi state, battery, sleep now, clean screen. (simulator; Sleep now on the device is a pending check)
- [~] **P7-7 Camera on the panel.** (2026-11-16: works with the C922 webcam: shots, video, compare, `around`; the Display lab ghost
  test and the Clean flash measured; exposure, white balance and focus locked with uvc-util, 30 fps video, the Ramp measured; open: the rest of the "To verify with the camera" list) A webcam pointed at the device: `make cam-shot`, `make cam-video`,
  `tools/cam.py compare` (ghosting number per region against the frame dump), `tools/cam.py around tap|swipe ...` (input + video +
  refresh log: refresh timing as the panel shows it). The set-up is not stable over days: register the panel on every run, compare numbers
  only within one session, never goldens (DISPLAY.md "Camera on the panel"). Done when the four commands have run with the real camera
  and the "To verify with the camera" list in STATUS.md has a first pass.

- [~] **P7-8 Somdoku** (the user's own daily puzzle, somdoku.nl). Read: apps/somdoku.md. Port of the
  web game's generator and game logic, checked against 500 days of vectors from the web code; app with ready / play / done
  views, score to the clipboard (no direct WhatsApp send). (2026-11-16: built and tested in the simulator;
  device run pending.)

- [~] **P7-9 Word game** (daily five-letter word game, "woordle"). Read: apps/wordgame.md, D26. An own
  game in the genre, not a copy of woordle.nl: own name, own words and day order. Game logic with unit tests, game and
  statistics screens, score to the clipboard. (2026-11-16: built and tested in the simulator; device run pending.)

- [~] **P7-10 Agenda**. Read: D28, COMPANION.md 7.1 `agenda`, apps/agenda.md. Phone: read the visible calendars (`READ_CALENDAR`, switch
  "Share the agenda", off by default), pack 14 days into `agenda` runs, send changed days, the whole window daily. Paper:
  `core/agenda` (store, apply, next line), the Agenda app (Day view with time blocks, List view), the line on the launcher and
  the sleep screen. (2026-11-16: built; simulator, host unit tests and the Android JVM tests; not run on the phone or the device.)
  Done when: a real calendar event made on the phone shows on the Paper within two minutes of a relay read, and an edit and a
  delete follow.

- [x] **P7-11 Weather**. Read:
  apps/weather.md, BACKEND.md "API calls used". App `weather`: `weather.thuis` state for now (condition, temperature, feels like,
  wind, humidity, rain chance), `weather.get_forecasts` (`?return_response`, daily and hourly) for the next hours and days,
  `sun.sun` for sunrise and sunset; three pages (now, today and tomorrow in blocks of three hours, ten days; D29); saved copy
  first, then a read; not set up / token refused / entity missing / unreachable / stale handled visibly. Condition icons from Lucide. Fixtures, UI scripts
  `weather*`, unit tests. Done when: goldens accepted, `make check` green, the live entity read on the device. (2026-11-16: done;
  live read on the device: 10 days, 24 hours, STATUS.md.)

- [x] **P7-12 Teletekst**. Read: apps/teletekst.md, D32. App `teletekst`:
  NOS Teletekst from the JSON its own site reads (`teletekst-data.nos.nl/json/<page>`), drawn as the 40 x 24 grid in ink
  and paper with a mono font; a tap on a row opens its link, swipes and arrows for subpages, the page's four coloured links
  as buttons, previous / next page, a number pad; saved copies of the last 16 pages (shown first, and when a read fails).
  Fixtures with made-up pages, UI scripts `teletekst*`, unit tests. Done when: goldens accepted, `make check` green, a
  live page read on the device. (2026-11-18: built, tested in the simulator, flashed and read live pages on the device; the panel's look is a pending camera check.)

- [x] **P7-13 Idle return to the launcher**. Read: apps/launcher.md "Idle return", D33. The Shell
  leaves an app nobody touched for `launcher.homeAfterSec` (default 60; Settings > Power); not Read, Somdoku or the word
  game (`.stayOpen`), not a screen that says `staysOpen()`, not while the keyboard shows. Unit tests, UI script `idle-home`.
  Done when: `make check` green, seen on the device. (2026-11-18: done; on the device Settings went to the launcher after
  60 s, the reader stayed.)

- [x] **P7-14 Hacker News**. Read:
  apps/hackernews.md, D34. App `hackernews`: the Top / New / Ask / Show lists (thirty stories, six a page) from the site's
  API (order) and its search service (content), a story's comments as pages of text with replies moved right, "Next
  thread", the article a story links to as text from a reader service (`r.jina.ai`), the link as a QR code, saved copies of the lists, the open
  story or article back at its page after the idle return. Fixtures with made-up stories, UI scripts `hackernews*`,
  unit tests. Done when: goldens accepted, `make check` green, a live list, thread and article read on the device,
  speed and flicker checked with the camera.
  (2026-11-18: done; on the device a thread of 236 comments, an article of 145 paragraphs; camera: page turns and list
  changes are one Fast pass, 105-120 ms from the tap to the panel driver, apps/hackernews.md "Speed and flicker".)

- [x] **P7-15 Inter drawn as wide as it is measured** (2026-11-18, from STATUS "Notes for later tasks": found with the
  Hacker News app). Read: D24, UI.md "Type". M5GFX patch 5 in `tools/patch_m5gfx.py` (`BFFfont::drawChar` rounds a
  fractional advance as `updateFontMetric` does), in every environment; `ui::penAdvance` and the text block's special
  case for Inter go; every golden with Inter text re-accepted after a look. Done when: a unit test proves measured
  width = pen advance for every baked character in every face, `make check` green, the EPD self-test says identical on
  the device, docs true. (2026-11-18: done; on the device the EPD self-test says identical for Fast and Quality, frame dumps of the
  test screens, Settings and the launcher are the simulator's pixel for pixel.)

- [x] **P7-16 Today board on the sleep screen**. Read: POWER.md "Sleep screen" and "Scheduled
  refresh", D28, apps/weather.md, apps/agenda.md. The panel keeps its picture without power, and the device is off most
  of the day: the sleep screen shows the day instead of a logo. From the top: the date; the weather of the first place
  (today's high and low, condition, rain chance, the next four blocks of three hours) from the saved copy; the agenda's
  next events (today's that are not over, then the next day's, five rows); the open book as one row (cover, title,
  percent); unread counts per app (counts only, as before); "Powered off" and the hint as they are. Forecasts and clock
  times only, nothing that reads as "now": the screen stays for hours. A section with nothing to show is left out; with
  neither weather nor agenda the screen is the one it was (cover or mark). `core/sleep_screen` draws a plain
  `SleepBoard`; the weather part is filled by the weather app's model through a source set in `apps/apps.cpp` (core
  does not include apps). The scheduled refresh also reads the weather (`apps/weather/weather_fetch`, shared with the
  app) and redraws when the relay or the weather answered. Settings > Power: the wake's label names the sleep screen,
  and a switch "Today on the sleep screen" (`power.sleepBoard`, default on). Done when: unit tests for the board's rows
  and blocks, UI script `today-board` with goldens (power-off, scheduled refresh), `make check` green, a frame dump of
  the board from the device and one RTC wake that redraws it. (2026-11-18: done; on the device the board was read back
  through the test screen `sleepboard` with live weather and agenda, and one RTC wake logged "weather read"; the
  picture on the panel itself is a pending check: the camera pointed elsewhere.)

- [ ] **P7-17 Home Assistant states on the Today board** (follows P7-16; ask the owner which). Two or three states from
  `home.json` (thermostat, a door, the car) as a row on the board: needs a way to mark the items (a key in `home.json`),
  their reads in the scheduled refresh, and a text per item type.

- [~] **P7-18 Read later**. Read: COMPANION.md 7.1 `clip`, apps/hackernews.md "The article", D34,
  apps/reader.md. A link shared with the Paper ("Send to Paper" on the phone's share sheet: a `clip` whose text is one
  link, alone or after a title; no change to the phone app or the protocol's messages) is queued
  (`core/readlater`, `/data/readlater.json`) besides becoming the clipboard. The page is read through the reader
  service the Hacker News app uses (D34: the address goes to that third party) and saved as it answers to
  `/sd/books/Articles/<title> - <host>.md`: a book like any other. The Read app opens `.md` (Markdown through
  `hackernews::parseArticle`: headings, lists, bold), lists new articles first with "New", and removes one on a long
  press. Pages are read by the scheduled refresh and when the library opens (not at the launcher's relay check: the one
  worker would keep an app opened meanwhile waiting); a link that cannot be read is dropped after a note (no text,
  refused) or after six tries (no network). Links that wait and articles nobody opened are the Read tile's badge and so
  the Today board's count; the launcher's and the plain sleep screen's "N unread" stay
  messages only. Done when: unit tests (the link rule, the queue, file names, the `.md` import), UI script with goldens
  (a shared link arrives, the library, the article, removing it), `make check` green, a link shared from the real phone
  read on the device. (2026-11-18: built; on the device a pushed article is listed, read and removed, and a link shared from the real
  phone came through, but its site (tweakers.net) refuses the reader service: no article. Open: a real shared link that
  becomes an article.)

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
| WhatsApp unread | P6-8 to P6-11 (companion app, encrypted relay through Home Assistant, D22) |
| WhatsApp send, clipboard | P6-7 clipboard (D21); replies through the phone, P6-10/P6-11 (D22); clipboard sync, P6-12 (D23) |
| Recent mail | P6-8 to P6-11 (Proton Mail notifications: sender, subject, maybe a preview; D22) |
| Home Assistant control | P5 |
| Agenda | P7-10 (the phone's calendar, one way through the encrypted relay, D28) |
| Weather | P7-11 (Google Weather through Home Assistant) |
| Teletekst | P7-12 (NOS Teletekst, read from the service's own JSON) |
| Hacker News | P7-14 (lists and comments from the site's API and its search service, articles as text from a reader service) |
