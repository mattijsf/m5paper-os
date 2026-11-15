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

## What works today

| Thing | Command | Result |
|---|---|---|
| Simulator build | `make sim` | OK (first build ~3 min, then seconds) |
| Headless screenshot | `make shot` | `out/shot.png`, 540x960, 16 grays |
| Simulator window | `make run` | mouse = touch, S screenshot, G ghost overlay, L refresh log |
| Firmware build | `make fw` | OK, 1.45 MB (27.7% of the 5 MB slot) |
| Unit tests / UI goldens / gallery | `make test` / `make ui-test` / `make gallery` | 93 tests; 55 goldens; `out/gallery.html` |
| Flash | `make flash` | OK over `/dev/cu.usbmodem101`, ~18 s |
| Device frame dump | `make device-shot` | `out/device.png`, pixel-identical to the simulator's frame |
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
| Home Assistant URL | The public HTTPS address, set by the user as `HA_URL=` on the card; not hard-coded or committed; works away from home. TLS cost: one kept-alive connection per refresh | D18 (note), BACKEND.md "Connection" |
| Home Assistant token, during development | The token on the card stays as it is; after development it is replaced by a token of a dedicated non-admin user. Code stays within the non-admin API subset (per-entity `GET /api/states/<id>`, service calls; no `/api/template`, no event firing) so the swap needs no code change | D18 |
| Books | SD card is in, empty: find open books ourselves | `tools/fetch-books.sh`, task P4-0 pushes them to the card |

## Not verified yet (do not assume)

- **Reader on the physical panel (P4-6 stays open)**: ghosting after 10 and 30 page turns (the Quality clean cadence of the ghost
  budget, `ghostLimit`), how the Fast pass then 16-gray settle looks at the four text sizes (is S 24 px still comfortable, is the
  bold double strike at S/L/XL acceptable), the cover thumbnail's gray quality, the flash feel of "Clean", the menu sheet's feel,
  and the real page-turn time as felt. The refresh log says a turn is ~150-210 ms render + 87 ms push call + about 0.3 s of waveform.
  Needs a person's eyes once, batched.
- **What the panel physically shows after Fast then settle** (ghosting, edge quality of the
  touch marker, flash feel): no tool can see it. Frame dumps show the firmware's frame only.

- **What the panel shows**: the user saw the mock launcher well enough to aim at the Settings
  tile, but nobody has confirmed the gray ramp order, sharpness, ghosting, or that the black
  dot appears under the finger. Ask once, or have the user send a photo.
- Light-sleep current, touch wake from light sleep, power-off and RTC wake (task P3-1).
- Whether a side-button click is readable by a running program (task P3-1).
- Home Assistant: entity ids come from the dashboard repo, not from the live instance; nothing
  has been requested from the live instance yet (tasks P5-1, P5-2).
- Which Home Assistant user the token belongs to. The Home app and the inbox calls have **not been run with a non-admin token** (D18 assumes they work); the swap to a dedicated non-admin user comes after development.
- WhatsApp add-on: installed, **not linked yet**; event fields for groups and the effect on phone
  notifications are unknown (task P6-1, see "P6-1 progress"). `ha/paperos_inbox.yaml` is a copy of the live WhatsApp package (tested with fake events only; see P6-3).
- Proton Mail Bridge on Home Assistant OS: no confirmed add-on that exposes IMAP (task P6-2).

## P6-1 progress (2026-11-15, done with the owner in Home Assistant)

- Add-on repository `https://github.com/FaserF/hassio-addons` added; "WhatsApp Gateway" 2.1.10 (slug `605cee21_whatsapp`)
  installed, running, start on boot, auto-update off. Options: `admin_notifications_enabled` false, `admin_numbers` empty,
  `mark_online` false, `welcome_message_enabled` false, `sync_full_history` false, webhook off. The add-on wrote its integration
  2.1.10 to `custom_components/whatsapp` (`auto_install_integration`). Add-on facts: host network, port 8066, `/config`
  read-write, API key shown in its web UI (BACKEND.md).
- The user accepted the terms-of-service risk on 2026-11-15 ("Install the add-on it is fine").
- **Update 2026-11-15 19:51**: the WhatsApp link is up (QR scanned in the add-on's web UI; the session survived an add-on restart). Facts and warnings (number pairing rewrites 0 to 49, the setup dialog voids a shown QR, Baileys 7.0.0-rc14 pairing bugs, no `ui_auth` = every private-network address is superadmin and the API key goes out over mDNS) are in BACKEND.md. Options now also `log_level` info, `mask_sensitive_data` true.
- **Update 20:12-20:14**: the integration is set up (API token pasted by the user), connected, Home Assistant restarted, `sensor.paperos_whatsapp_inbox` restored, recorder exclusion verified (BACKEND.md "Integration set up"). **Still not verified**: a real incoming WhatsApp message in the feed (P6-1 goes to [x] when one is seen), the clear script with a non-admin token, the 48 h expiry, group names.
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

- Power-off and resume (2026-11-15, `>poweroff 20`, now calling `prepareForPowerOff()` like auto-off, RTC wake only): the board lost power 2 s after the command and the port came back 22 s after it
  (the 20 s alarm); the device answered 24 s after the command. The reader reopened Alice on the same page: the frame dump after the boot is
  **identical** to the one before the power-off (crc ebda3f52); `Reading: ...` shows in the launcher summary line on the device. The book opened
  1.6 s after the boot (cached index and the chapter converted from the EPUB).

- Status bar Wi-Fi icon (2026-11-15): the old Shell cleared the new Shell's net callback when a screen change replaced it (`set screen`, `restart`), so the icon stayed off while connected. Fixed (`NetService::onChangeOwner`); on the device the icon is on 14 s after a fetch (`out/dev-wa-14s-fixed.png`). That frame also shows a real WhatsApp message in the feed (a test message, 20:24), seen on the device at 20:27.

## Pending device checks

- **P7-6**: "Sleep now" ends in the sleep screen and a power-off (not run: only the side button restarts it). The swipe from the status bar works on the device (checked 2026-11-15).
- **P6-4/P6-5**: nothing of the inbox has run on the device or against real feeds: the feed sensors do not exist yet (P6-3). Unverified
  assumptions: the exact `imap.fetch` response shape (`service_response.text`), the optional `connected` attribute for the link-down
  notice, that a non-admin token may call `script.paperos_inbox_clear`, and the fetch with a real entry id and uid.
- **P5-5**: one run with a non-admin token before Phase 5 counts as done; toggle timing on the device (tap to visible change under
  0.4 s; not run, a toggle changes the user's real lights); app open with Wi-Fi off to fresh data (under 4 s: warm Lights tab measured
  1.4 s, cold adds the Wi-Fi join and TLS, about 2 s: not measured); whether polling needs the Wi-Fi linger extended.
- **P5-1**: Settings > Home Assistant "Test connection" now goes through `HaClient` (`core/ha`); re-run it on the device (it worked with the previous direct request, P3-7).
- **P3-6**: how the sleep screen looks on the panel (cover gray ramp, contrast, that it stays sharp for hours: a frame dump cannot show it; the device was powered off right after drawing it); button press to usable screen (an RTC wake measured instead, see below; a real side-button press not timed).
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

1. ~~Where may WhatsApp data live? / add-on risk~~ **Closed 2026-11-15**: a hardened Home Assistant sensor (DECISIONS.md D20); the terms-of-service risk was
   accepted at install; the add-on's unauthenticated endpoints (BACKEND.md "Add-on security") are an accepted risk, "Keep running". **Watch**: check each add-on
   update for auth on `/api/autoresponder/config` and `/api/dashboard`; the add-on's API token never goes on the SD card.
2. **Wi-Fi**: add `WIFI_SSID=` and `WIFI_PASSWORD=` to `paperos_config` on the card? It lets
   the device go online from task P3-2, long before the on-screen keyboard exists.
3. **Proton**: paid plan (needed for Bridge)? OK to run Bridge as a Home Assistant add-on?
   (task P6-2)

None of these block Phase 1, Phase 2, or the reader.

## Notes for later tasks

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
- 2026-11-15: P7-6 quick panel (`core/quick_panel`, `Shell::sleepNow`); Home light sheet shows the brightness value.
- 2026-11-15: launcher summary line (`summaryLine`, golden with and without data).
- 2026-11-15: P6-3 (WhatsApp half, done in parallel): `ha/paperos_inbox.yaml` synced from the Home Assistant repo; BACKEND.md event fields and feed mapping corrected; D20; add-on security facts.
