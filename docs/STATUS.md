# Status

Where the project stands: what works, the numbers that matter, what still needs a device check, and the pitfalls to know
before the next task. Update it at the end of every task (ROADMAP.md "How to work a task").

## Now

- **Phases 0 to 2 are done.** Phases 3 to 7 are built; the feature cards through P7-24 are done or dropped. The cards
  still marked `[~]` or `[ ]` in ROADMAP.md wait mostly on device or camera checks: P3-5, P3-6, P5-5, P6-7, P6-11,
  P6-12, P7-1, P7-7, P7-8, P7-9, P7-10, P7-19.
- **Next task: P3-5 power measurements** (the first open card from the top): battery sessions with `/data/power.csv`,
  the 80 MHz CPU experiment, POWER.md estimates replaced by numbers. There is no current meter; every mAh figure in
  POWER.md is an estimate.
- **Firmware**: 2,671,959 bytes (2.55 MiB, 51 % of the 5 MB app slot, 473,769 bytes under the 3 MB budget of
  ARCHITECTURE.md).
- **Tests**: 384 host unit tests (`make test`), 334 UI screenshots compared with goldens (`make ui-test`), JVM tests for
  the companion app (`android/`).

## What works

State per area. "Device" means run on the PaperS3 with injected input, frame dumps and the log; "camera" means also
checked on the physical panel with the webcam (DISPLAY.md "Camera on the panel"); "simulator" means not on the device yet.

### Platform and display

- HAL with two platforms (`src/platform/device`, `src/platform/sim`); shared code is platform-free. Device frame dumps
  are pixel-identical to the simulator's for the same input (static test screens, Settings, launcher, reader pages).
- DisplayManager (DISPLAY.md): Fast then settle, ghost budget with Quality cleans at navigation, `Hint::Page` (one Fast
  push, a Quality clean every 10th page, `display.pageCleanEvery`), one Text push for screens with flat gray areas
  (`grayAreaMin`), Feedback and Content pushed per band of changed rows, a page push keeps older settles outside it.
- M5GFX patched at build time (D24, `tools/patch_m5gfx.py`, five patches): tiled frame-buffer write on both cores,
  finished rows skipped, unchanged pixels not driven again (patch 4: the white background no longer darkens in every
  pass, camera), Inter glyphs drawn at their measured advance (patch 5). `>set epd-selftest on` says "identical".
- Fast text path (byte-identical to LovyanGFX), fonts Inter, Literata, JetBrains Mono (Teletekst), Lucide icons and
  hand-drawn icons (`assets/icons-drawn/`). Emoji from atlases on the card (Fluent High Contrast, Noto for the rest,
  1891 in all; flags as country letters in a box), reloaded when a file is replaced. Device: identical to the simulator.
- Clean gesture: swipe up from the bottom 60 px = one full-screen Quality pass (device, camera).
- 120 MHz flash and PSRAM, ESP-IDF built from source (D25). Soak-tested clean.

### UI kit

- Widgets, layout, sheets, toasts (an ink band over the status bar), paged lists, keyboard, text field with the clipboard
  sheet, TextBlock with pagination, QR codes, images (cover decode on the worker). UI.md has the tokens and limits.
- Pressed looks appear only after 120 ms of holding (`kPressDelayMs`), so a quick tap costs no extra pass.
- App framework (`core/app`, Shell, store, `ctx.async`, `ctx.background`, `ctx.http`, `AppContext::link`), APPS.md.

### Power

- Light sleep between touches on battery (touch wakes it), stays awake on USB; USB seen this boot suppresses auto-off for
  12 h (`kUsbGraceMs`); a host seen in the last 10 minutes counts as connected (`kUsbHostHoldMs`).
- When unused (D38): after `power.autoOffMin` the sleep screen (default) or the launcher. On battery that is a power-off
  with the sleep screen on the panel; on USB or with wireless development on, the sleep screen stays with "Tap to start",
  is redrawn when its content changes, and a tap on a part opens that app there (device).
- Sleep screen / Today board (D35): date, weather of the first place, agenda, open book, unread counts (device, camera).
- Scheduled refresh (off by default, `power.refreshHours`): RTC wake, relay and weather read, board redrawn, power-off
  again (device, camera; the hour-long alarm on battery is not run).
- Power-off and resume: the last app and the open book come back on the same page (device).
- Remote traffic counts as use (`Os::keepAwake`): no sleep screen in the middle of an update.

### Network

- Wi-Fi on demand: fast path with the stored BSSID and channel, scan path as fallback, 20 s linger, `ctx.keepWifi()`
  (Teletekst), multi-network secrets on the card, Settings > Wi-Fi scan (paged, three scans merged).
- SNTP to the RTC, local time zone kept (Arduino's `configTime` would set UTC); waits for SNTP's own notification.
- HTTPS client only, 44 common root certificates, one kept-alive TLS connection. A request that failed before anything
  left the device is sent once more (`hal::HttpResponse::notSent`); `tools/device.py status` shows the last failure.
- Home Assistant client (`core/ha`) inside the non-admin API subset (per-entity `GET /api/states/<id>`, service calls;
  no `/api/states`, `/api/template` or events). Has not been run with a non-admin token yet.
- Update over Wi-Fi (D37): Ed25519-signed package, written to the other slot, SHA-256 checked, bootloader rollback if
  the new image is reset within 15 s (device: update, confirmation, rollback, refused packages).
- Wireless development (D39): remote commands over Wi-Fi on port 8266 after a signed sign-in, `make ota` as `>ota`,
  `--wifi` for `device.py`, `view.py`, `push.py`. Keeps the device on while enabled (device, on battery).

### Apps

- **Launcher**: 3 x 4 tiles, all twelve apps on page 1, date, next agenda event, summary line, badges, power button in
  the header (sleep screen, then off). Reads the phone relay when its last read is over 10 minutes old (device; the tap
  on the power button itself is not run).
- **Settings**: Wi-Fi, Display, Power, Reading, Home Assistant (with Phone pairing), Storage (Files over Wi-Fi), Developer
  (Wireless development, Status, Display lab), About (build id). Device.
- **Files over Wi-Fi** (D30): WebDAV and an upload page behind a one-time code, while the page is open. Device with curl
  and a WebDAV client library; Finder and Android not tried.
- **Read**: EPUB, TXT and Markdown; library with covers and "New" articles; menu (size, contents, go to, clean); position
  saved. Read later (D36): a link shared from the phone becomes `/sd/books/Articles/<title> - <host>.md` (device, two
  real links). Pages come from Jina Reader, then markdown.new as fallback (D34).
- **Home**: Lights, Climate, More, Music tabs from `home.json` (version 5, example house by default). Device: all
  entities read, Music tab reads every 5 s, cover drawn, `volume_set` works. Toggles and scenes not sent from the device.
  The owner's list is on the card (`/sd/paperos/home.json`, device 2026-11-27); a one-off firmware move put it there
  from the old internal copy and was then taken out again.
- **WhatsApp and Mail** (inbox): encrypted notifications from the companion app through the Home Assistant relay
  (D22, COMPANION.md). Device: real messages, badges, "Mark all read" back to the launcher. A reply from the Paper has
  not arrived end to end yet. Sound for new messages: simulator only.
- **Agenda** (D28): the phone's selected calendars, Day and List views, next event on launcher and sleep screen
  (device; edits and deletes following through not checked).
- **Weather** (D29): Google Weather through Home Assistant, two places (`weather.thuis`, `weather.office`), three
  pages, rain graph from the minute forecast. Device (live rain in the graph not seen yet).
- **Teletekst** (D32): 40 x 24 grid in ink and paper, saved pages, number pad. Device and camera. Opening on page 101 is
  simulator only.
- **Hacker News** (D34): Top / New / Ask / Show, the article first, comments as pages, QR link. Device and camera.
- **Somdoku**: daily puzzle, generator matches 500 days of the web game. Device: opened to the ready screen only.
- **Word game** (D26): daily five-letter game, accepted words fetched weekly from woordscore.nl. Device: played through,
  camera stills.
- **Chess** (D41): offline engine (levels 1-4, 20,000-node limit) and the Lichess computer (Board API, `LICHESS_TOKEN`
  on the card). Device: engine timed, one Lichess game played and resigned.

### Companion app (`android/`)

- Notification listener for WhatsApp and Proton Mail, X25519 key in the Android Keystore, QR pairing with a 6-digit code,
  envelopes through the relay (ciphertext only in Home Assistant). Paired and delivering on a real phone.
- Agenda sharing with per-calendar switches; clipboard both ways (share sheet "Send to Paper" seen on the Paper through
  Read later; Paper to phone not checked); replies through the notification's RemoteInput (off until enabled in the app).
- Maintenance: Remote control (screen and touch only) and Firmware update (`make ota-phone` package). Done on the device.

### Tooling

- Remote control over USB serial or Wi-Fi (`tools/device.py`: tap, long, swipe, shot, log, refresh, status, set, ls, cat,
  rm, reboot, poweroff, script), live view (`make view`), UI scripts against goldens on the device (`device.py script`).
- Camera (`tools/cam.py`): lock exposure, calibration target, shots, video, `compare`, `around tap X Y`.
- `tools/latency.py` (`[lat]` medians), `tools/memory.py` (heap and PSRAM per app scenario), `tools/push.py`,
  `tools/ota.py`, `tools/new-app.sh`, `make gallery`.

## Measurements

Latest numbers only. Device unless marked.

### Latency (finger up to the update handed to the panel driver, medians, `tools/latency.py`)

| Interaction | Time |
|---|---|
| Reader page turn | 105-112 ms (slowest of 60: 159 ms); on the panel first change 0.38 s, done 0.55 s (camera, includes ~0.2 s camera delay) |
| Open a screen (Settings row) | 150 ms |
| Back to the launcher | 15 ms before the first pixel, 137 ms tap to panel |
| Open WhatsApp / Mail | 18-19 ms before the first pixel, 179-197 ms tap to panel |
| Small control (stepper) | 5 ms |
| List page turn / segmented switch (Hacker News) | 105 ms / 120 ms, one Fast pass each |
| Teletekst page | 189-295 ms from the network, 130 ms from a saved copy |
| Chess selection / move | 51 ms / 194 ms to the last push |
| Live view click to new frame | ~760 ms (USB) |

### Panel refresh (`>set epd-timing on`)

- Fast push 246-250 ms whatever the size; Text 820-824 ms; Quality full screen ~1.36 s; Quality flash visible ~0.66-1.1 s.
- A typed key: Fast push of its tile, settle 0.6 s later. A Fast pass is the driver's ordered dither (camera).

### Ghosting (camera, numbers valid within one session only)

- Reader: 1.1-1.3 % of contrast, flat over 30 pages with the clean every 10th page (session floor ~1.1 %).
- Teletekst after eight turns 0.86 %; 20 navigations with patch 4: 0.75 % (stock driver rule 1.09 %).
- Gray ramp is monotonic; g0..g6 crowd near ink, g12..g15 near white, big step g11 to g12. Mid grays print lighter
  than the simulator shows.

### Reader

- First open of an EPUB (312-609 KB): 10-16 s, mostly layout (text measuring 40-55 %). Cached open 30-230 ms, text on
  screen 0.7-1.4 s. TXT 181 KB: 1.5 s cold. Markdown article: ~2 s first open.
- No 1 MB EPUB measured (extrapolated 20-25 s).

### Network

- Wi-Fi join: 1.2-1.7 s fast path, ~5.3 s scan path. TLS handshake 0.5-1.5 s depending on host; requests on the kept
  connection 34-60 ms.
- Home Lights tab: 18 reads in 1.4 s on one connection. Music tab read 36-225 ms.
- Reader service: 1.9 s for a readable page, up to 16.9 s for a site that refuses it. Hacker News article of 145
  paragraphs 1.6 s.
- Word list fetch: TLS + TCP 1.5 s, request 1.4 s, body 0.1 s.

### Memory (`tools/memory.py`, D27)

- PSRAM 8 MB, ~5.4 MB free with an app open.
- Internal heap: ~115 KB free at idle; lowest 69-83 KB during a TLS read (Home, weather, Teletekst, Hacker News); back
  to idle after leaving every app measured. A book open drops it by ~30 KB (chapter text as small strings).
- Stacks: UI loop task 16 KB, worker 16 KB (10.5 KB left in Hacker News).

### Boot, wake and transfer

- Scheduled wake: 4.0-4.2 s awake with relay and weather (weather's five requests ~0.25 s).
- Resume into the open book: 1.6 s after boot. Button press to usable screen: not measured (P3-6).
- USB push 110-131 KB/s; SD card writes ~90 KB/s. Update over Wi-Fi: 2.5 MB in 25-47 s. Frame dump over Wi-Fi
  0.4-0.9 s, over USB ~0.5 s. `make flash` ~18 s.
- Chess engine: ~0.3 ms a node, up to ~5.5 s a move at level 4.

### Power

- Battery 1800 mAh. Only estimates (POWER.md): a scheduled wake ~0.2 mAh, under 1 % a day at a 1 h interval.
  Light-sleep current and battery life are unmeasured (P3-5).

### Firmware size

- 2,671,639 bytes, budget 3 MB (3,145,728) of the 5 MB slot. D31 lists what is left to take; fonts (~473 KB) first.

## Pending device checks

### Display and panel (camera)

- Fast then settle in general: `cam.py around tap` on a toggle and a list row; the edge of the touch dot; the Clean flash
  as video.
- Screens that are still one Text pass (Agenda day view, keyboard, book cover): number of waveforms, time to final look,
  ghost after a Text push over a Fast image. If too slow, `grayAreaMin = 0` restores Fast then settle.
- Gray levels in real screens: gray(9) dividers in a list, gray(3) metadata, the Agenda's gray(12) blocks and ink bars.
- Toast band: one Fast push of 540 x 56, status bar back after 3 s, no ghost left in the status bar.
- Emoji at 18 and 22 px on the panel (`>set screen emoji`); a real message with emoji.
- Sleep screen sharpness after an hour and after several hours off.
- The idle switch to the launcher or sleep screen, as it looks on the panel.
- Word game: the lost screen; used-up keys (gray(10)) were faint on the camera.

### Power

- Real touch wakes light sleep and the waking tap is delivered (an injected tap cannot wake the chip).
- `powerOff(0)` and the side button bringing the device back; "Sleep now" from the quick panel.
- Idle on battery: the idle timer must wake the chip from light sleep. Unplug, open Weather, leave it; after plugging in
  the frame dump shows the expected screen and the log says the app was not used.
- "Tap to start" battery case: wireless development off, no USB for 12 h; the idle time must end in "Powered off".
- Scheduled refresh with a 1 h interval on battery, set in Settings (only `>poweroff N [M]` with 20-30 s alarms ran).
- A scheduled wake as the first boot of a new firmware (it powers off within 5 s: `confirmUpdateIfPending`).
- A side-button press within -90 s .. +180 s of the alarm runs the refresh first (simulator only).
- Auto-off guard: an idle device on USB with no program holding the port stays on past 20 minutes.
- Boot without an SD card logs `no SD card` and continues.

### Network and update

- Update over Wi-Fi with the device away from USB, using the saved address (`tools/ota.py host`).
- Heap while the update page receives; a new firmware that crashes by itself in its first seconds rolls back.
- The second try of a request: after a "Home Assistant not reachable" toast, read `last failed request` in
  `tools/device.py status` before the device powers off. "second try answered" confirms the fix; "failed too" or
  "not sent again" names the real cause.
- Clock drift over a day (the daily sync logs `clock was N s off`).
- With the trimmed firmware (D31): a Wi-Fi reconnect after sleep, a night of normal use (assertions are silent: a crash
  shows an address only).
- Settings > Home Assistant "Test connection" through `HaClient`.
- Files over Wi-Fi from Finder (Go > Connect to Server), an Android WebDAV client, Chrome on a phone, and a 20 MB upload
  for the card-write retry rate.

### Home Assistant and Home app

- One run of Home and the inbox with a token of a dedicated non-admin user (P5-5).
- Toggle timing (tap to visible change under 0.4 s) and a cold open with Wi-Fi off (under 4 s target).
- Music tab calls from the Paper: `volume_mute`, `media_play_pause`, next / previous, `shuffle_set`, `repeat_set`,
  `select_source`. The `[home] media` and `[home] call` log lines show what was sent and reported.

### Inbox, companion and clipboard

- A reply typed on the Paper arrives on the phone (enable "Allow replies from the Paper" in the app first).
- Mark all read with messages present, also while a relay read runs: the launcher shows no badge, the log says
  `[inbox] mark all read: dismiss for N notification(s) to the phone: pushed`; reopen the app: no rows. Same in Mail.
- Sound: `tools/device.py set chime 1` (2093 Hz then 2794 Hz, `kChime` in `platform/device/sound.cpp`): audible, pitch
  right, pin quiet afterwards. Then a real message: `[sound] N new message(s)`; with `>poweroff 30` the chime ends before
  the power goes.
- `paired` resend: on the next pairing the log shows `paired to the phone: pushed, N s` and
  `/data/companion_paired_owed` is gone; with the relay unreachable at Confirm the phone activates once Wi-Fi is back.
- Clipboard: a copy on the Paper reaches the phone within a minute; text shared on the phone is offered by Paste on the
  Paper ("Clipboard from your phone"). Unknown: whether `setPrimaryClip` from the foreground service works on Android 17.
- Agenda: an edit and a delete on the phone reach the Paper after a relay read; switching sharing off empties the Paper;
  `tools/memory.py --only agenda`.

### Apps

- Teletekst opened after more than an hour logs `[teletekst] page 101`.
- Read later: an article saved by a scheduled wake while off (`[sched] ... N article(s)`, seventh field of
  `/data/refresh.log`); the toast "The site refused the reader (403)".
- Today board: the book row (the `sleepboard` test screen passes no book), the Settings > Power switch.
- When unused: a tap on the weather and on an unread count of the board; "Then show: Home".
- Keyboard while typing a Wi-Fi password: per key Fast pushes of about 48 x 66 and 16 x 32 (`tools/device.py refresh`),
  no box from the field down to the key; `cam.py around` for the time per key. Reported slow and unresponsive on battery
  (2026-11-27): each key woke the chip from light sleep; the keyboard now holds light sleep off for 10 s after a key.
  Also reported: no fast feel and rough refresh boxes. A key going up was Fast (gray fill as dither) then a Text settle;
  now one Text push of the key (`why=feedback-gray`, DISPLAY.md). Device, 2026-11-27 (USB, injected taps): a letter is
  one Fast push of 16 x 32 at the field, 8-11 ms from touch to push; shift and 123 are one Text push of the keyboard
  (504 x 166 / 534 x 236, 140-250 ms), no Fast first. Still to check: the pressed look with a finger (injected taps are
  shorter than the press delay), on the panel with the camera (no dither left on the keys), and typing on battery
  (speed, no dropped keys). Not the scan: the scan page starts no scan once it is covered,
  only one already running ends (2-3 s).
- Word game: the saved data of the old app id `vijfje` adopted (statistics keep their counts); `tools/memory.py --only
  wordgame`; the weekly word-list fetch with the 44-certificate bundle.
- Somdoku: play and solve a puzzle on the device; generation time on the worker; the clock across a light sleep and a
  power-off; a real flat-battery start shows "Klok zetten".
- Weather: the error screens (token refused, entity missing, unreachable) and the rain graph with live rain.

### For a person at the device

The camera cannot judge these; batch them into one look.

- Reading comfort: reader text at S and in italics between cleans, the bold double strike at S/L/XL, Inter 22 in Hacker
  News, whether 2 % of ghost contrast is visible.
- Teletekst: the cut ink-or-paper letters of the buttons, the 1 px rows of striped map blocks.
- Fingers: 153 x 160 launcher tiles, the 45 px word-game keys, typing on the keyboard, the delayed pressed look, the
  launcher power button, the Wi-Fi scan list while it grows.
- Daily use after patch 4: anything that looks off, such as patches of white.

## Open questions

- **Home Assistant websocket for the Home app.** `esp_transport_ws` exists and `subscribe_entities` works for a non-admin
  user; it would only run while Home is shown. Cost: a second TLS connection (~45 KB internal RAM of ~97 KB free), a
  websocket in the simulator fixtures, an amendment to D10. Not built; REST polling (5 s on Music, 30 s elsewhere) stands.
- **Inbox scope (D22).** Built on assumptions: replies only to chats with a live notification on the phone; Mail shows
  sender, subject and the notification's preview, read-only; the phone is mostly on and online at home, and both sides
  queue while it is away. Confirm or change.
- **Word game daily answers (D26).** The game uses its own answer list and order, so its word of the day differs from the
  web game's. Taking the web game's daily answers is not built.
- **Sites that refuse the reader services** (tweakers.net and other DPG Media sites, reuters.com, nytimes.com). Options,
  none built: the phone reads the page in a WebView with its own consent and sends the text through the relay in pieces;
  the site's RSS feed where it carries the full text. Which sites matter decides whether either is worth it.

## Notes for later tasks

### Build and toolchain

- **ESP-IDF core from source (D25).** A `custom_sdkconfig` change rebuilds it (~5 min, run in the background). PlatformIO's
  own Python needs `platformio==6.2.0` (not `6.2.0.post1`). After changing `custom_sdkconfig` or `board_build.f_*`, delete
  `sdkconfig.papers3` or the old values stay. `CONFIG_LWIP_IPV6=n` does not compile.
- **An edit of `platformio.ini` wipes all of `.pio/build`**, including simulator and test programs. To keep two builds in
  one checkout apart: `PLATFORMIO_BUILD_DIR=<dir> pio run -e sim` (and `pio test -e test --without-testing`), then copy
  `<dir>/sim/program` to `.pio/build/sim/program` before `tools/ui-test.sh`. Never build `papers3` while the core rebuilds.
- **M5GFX patches (D24)** run in every environment (`extra_scripts` under `[env]`), so the simulator and tests draw with
  the patched library. A fresh checkout patches its own `.pio/libdeps/<env>/M5GFX` on the first build; goldens made with
  an unpatched simulator will not match. A patch fails the build when the library text changed. On an M5GFX update the
  unit test "LovyanGFX moves the pen by the advance it measures" says whether patch 5 is still needed.
- **Firmware size (D31)**: `-fno-exceptions` (`try`/`throw` do not compile, a standard-library throw aborts), no
  `<sstream>`, `<iostream>`, `<fstream>`, `<regex>`, `<locale>` in firmware code (one include costs 100-160 KB; the
  simulator does not warn). An `-O2` in our flags has no effect: the build script's `-Os` wins.
- **`Store::get` / `set` with fixed-width integers**: `int32_t` is `long` on the ESP32 and ambiguous there; the simulator
  build accepts it. Pass `int`, `bool`, `double` or a string; cast first.
- `pio pkg exec -p tool-esptoolpy -- esptool` fails with "Permission denied"; call `~/.platformio/penv/bin/esptool`.
- `partitions.csv`: nvs must end before 0xE000 (otadata), app0 must start at 0x10000. The LittleFS partition is named
  `storage`, not `spiffs`.
- `make test` runs the test program itself: PlatformIO's doctest runner reports "SKIPPED" for a custom main.

### ESP32 runtime

- **The ESP32-S3 ROM has its own `tinfl_decompress`**, and `esp32s3.rom.ld` binds that name to a ROM address, so a vendored
  miniz silently uses the ROM copy with a smaller struct: heap corruption on the first EPUB entry. `lib/miniz/miniz_defs.h`
  renames ours to `pos_tinfl_*`. Check: `xtensa-esp32s3-elf-nm firmware.elf | grep tinfl_decompress` shows a `T` in
  flash, not an `A` at 0x4000xxxx.
- **The worker runs at priority 1 on core 0** (on the UI core it froze the UI for 17 s). A CPU-bound job there starves
  the idle task and the task watchdog resets the board: long jobs must call `cooperate()` (1 ms sleep every 40 ms, see
  `apps/reader/book.cpp`).
- **HTTPClient's body loop spins on a TLS read that returns 0**, which starves IDLE0 the same way. The clients in
  `platform/device/net.cpp` sleep 1 ms on an empty read (`YieldingClient`); keep that in any new client code.
- **Loop task stack is 16 KB** (`SET_LOOP_TASK_STACK_SIZE` in `platform/device/main.cpp`): an Ed25519 verify overflowed
  the Arduino default of 8 KB. ARCHITECTURE.md's "UI thread stack is 8 KB" predates this.
- **Internal heap is small.** Big caches are fixed tables in one PSRAM allocation, never many small nodes (a 40,000-entry
  `unordered_map` left 12 KB). D27 sends ordinary allocations and mbedTLS to PSRAM with a 32 KB internal reserve.
- **A blocking log stalls the UI.** `Serial.print` on USB CDC with no reader blocked the UI thread (first HTTPS request
  9.5 s instead of 2 s). Logging must never wait on the host.
- **Time**: Arduino's `configTime()` resets `TZ` to UTC; `Net::syncTime` keeps the zone and the RTC stays UTC. The system
  time survives a restart and drifts on the RC oscillator, so "time looks set" is not "synced": wait for
  `sntp_set_time_sync_notification_cb`. `CONFIG_LWIP_SNTP_STARTUP_DELAY` is off (it waited a random 0-5 s).
- **Unexplained restarts** (during a long HTTPS read while the Mac polled the log over Wi-Fi, once during an update, once
  in a Files over Wi-Fi test): the boot log prints no reset reason. Log `esp_reset_reason()` at boot, then reproduce. The
  7 s UI gap seen with the polling points at the radio lock in `net.h` (the remote server's socket work on the UI thread
  while the worker holds TLS).
- **Elapsed-time math with a stale clock reading wraps**: the remote server read `now` once per pass, and a connection
  accepted later had a start after it, so `now - start` wrapped and the 10 s sign-in timeout closed it at once. Read the
  clock where it is compared.
- **The side button belongs to the power chip** (PMS150G): firmware cannot read it; a double click powers off and leaves
  the last picture on the panel. Only a software power-off draws the sleep screen.
- **FAT and the Arduino SD library**: `SD.h` puts a global `fs` namespace in scope, so device code writes `pos::fs::`.
  A rename over an existing file fails on FAT: `pos::fs::writeFile` steps the old file aside first.

### USB, serial and device tools

- **Light sleep breaks the USB serial session** (input during sleep is lost, no wake from USB). On some hubs the device
  drops off the bus until the cable is replugged: never use `>set sleep` or `>set usbsleep` in an unattended run.
- **A Mac suspends the port when no program has it open**, so the device sees no host. This once caused an unattended
  power-off; hence the 12 h USB grace and the 10-minute host hold (POWER.md).
- **On battery, a boot that never saw USB powers off after the idle time**, ending wireless development until the side
  button. `>set idle-min N` changes the idle time until the next boot; `idle-min 1` on battery powers off after a minute.
  Set `idle-min 0` for long checks: an app opened for a check is otherwise left after the idle time.
- **Opening the serial port must not touch DTR/RTS** (`ser.dtr = False` reboots the board); plain `serial.Serial(port)`.
- `tools/device.py` opens the port exclusively; `make view` holds it, so stop it before other device tools. Do not keep a
  Link open and unread while doing something else (a later `>shot` went unanswered). After the first answer on a Link a
  command gets one 10 s try.
- `device.py` resends an unanswered command, so a slow device can run an injected tap twice.
- `>shot` answers after earlier touches are processed: send it alone. The device holds its log while a reply streams.
- `tools/device.py wait-boot` times out after an RTC wake: that boot logs "the panel keeps its image" instead of the line
  it waits for. The device is up.
- Injected input: a tap can be shorter than the 120 ms press delay (no pressed look); a long press sent right after the
  tap that closed a sheet does not open the sheet. Wait between them.
- UI scripts on the device: `launcher` and `display-lab` cannot match their goldens (live clock and counters, a stored
  book reopens); `widgets3` shots differ by refresh state only. The remote may not delete `/data/apps`.
- The webcam needs `cam.py lock` at the start of a camera session (in automatic mode the C922 reports defaults and the
  old lock froze a dark picture), then `make cam-target`. Capture runs from a Terminal window (camera permission). The
  set-up moves between days: compare numbers within one session only, never as goldens.

### Network

- **Never scan the home network for the device.** A sweep of all addresses on port 80 made the router block the Mac's
  connections to the device's port 80 until the next day. `tools/ota.py host ADDRESS` saves the address; updates and the
  remote use port 8266. The device should have a reserved address in the router.
- One kept TLS connection (`gSecure` in `platform/device/net.cpp`): every change of host costs a 0.8-0.9 s handshake. A
  second kept connection would help apps that talk to two hosts (Hacker News).
- "Home Assistant not reachable" right after a Wi-Fi join (`HaError::Other`) was seen but not reproduced in 16 opens. The
  one-retry in `NetService::perform` is built on the guess that the first lookup or connect on a new link fails now and
  then. Not tried: the open that wakes the device from light sleep, a DNS lookup not answered from lwIP's cache.
- Jina Reader answers a CAPTCHA page in 6 s with `X-Timeout: 6`; markdown.new allows 500 reads a day. Jina sometimes
  refuses GitHub for hours. Of two texts the longer wins.
- The Music tab's cover address (`entity_picture`, through Home Assistant's media proxy) carries an access token: never
  log or save it. The HA client's error log leaves out a path's query for this reason.
- If the Sonos switches to its TV input around a volume tap again, the `[home] media` / `[home] call` lines show what was
  sent; only `volume_set` was seen in the log when it happened once.

### Display and fonts

- LovyanGFX picks the colour format from the C++ type of the colour argument (`uint8_t` gives RGB332). `pos::Color` is
  `uint32_t` for this reason.
- M5GFX's BFF loader misreads sparse cmap subtables (icons as boxes); the generator rewrites the offsets (`patch_cmap`).
  Glyphs without a background colour have no anti-aliasing (UI.md "Pitfalls").
- Adding one icon to the Lucide font can shift every icon by a pixel (it once changed 277 screenshots). Look at the
  `fonts-sheet` diff and prefer an icon already in the font.
- A pressed look in flight delays what follows. Fixed for `PagerBar` and `Segmented`; not checked: `Stepper`, `LevelBar`,
  `TabBar` (keyboard keys draw on Down by design).
- Flat gray areas turn a screen change into a Text pass (more flicker); a change in pure black and white gets no settle.
  Prefer ink and paper for screens that change often (chess board hatching, struck-through clues, ink footer).
- Mid grays print lighter on the panel than in the simulator: gray(10) outlines nearly vanish, gray(12) is near white.
  UI.md has the gray limits.
- Screenshots show the emulated panel, including leftover grays from earlier passes. To compare what was drawn, use the
  script command `expect-frame-equal A B` (the display manager's shadow).

### Storage

- SD card writes are slow (~90 KB/s) and can fail with EIO while Wi-Fi receives: write less, retry card writes in
  servers. The reader converts chapters on demand and writes the cover after the book opens.
- `core/fs` keeps small `/data` files in memory: every write, append, delete and rename must go through it.
- Emoji atlases replaced while running are noticed by `fs::changes()` and reloaded.

### Simulator and UI tests

- A flaky shot gets a longer `wait`, never a new golden. The worker runs in real time while the script clock is fake, so
  its answer lands at a slightly different simulated time.
- `agenda-launcher` can catch the launcher before or after the settle that follows the relay read when the Mac is heavily
  loaded. The golden is the state before the settle.
- Two `make ui-test` runs in one checkout clobber each other's `out/ui`.
- `tools/device.py --sim --sim-data DIR` gives the simulator a card with a Wi-Fi network; `wifi hiccup N`, `wifi slow MS`,
  `wifi crowd N` exercise failure and scan paths.

### Code left for later

- Article code (`articleRequest`, `parseArticle`, `readPage`) lives in `apps/hackernews/hackernews_model.h` and is used by
  `apps/reader`; it belongs in `apps/common` with its tests.
- From the code review, not done: split `settings_app.cpp` (1,200 lines), `book.cpp` and `wordgame_app.cpp` by concern;
  one shared serial module for the Python tools; shared fake HAL and temp-card fixtures for the unit tests; `HaError`
  derived from message text in `core/ha.cpp`; the inbox's "seen" marker stored as an `int` (2038); "Clear caches" also
  deletes the decrypted notifications.
- Still on the UI thread: the inbox parses its saved notifications on open; key derivation for "Mark all read", a reply
  and the clipboard push run on a tap.
- A reply of ~900 non-ASCII characters does not fit the 1000-byte envelope; it is refused ("does not fit") instead of
  counted in bytes while typing.
- `whatsapp::sendToFavorite` (`apps/inbox/compose`) has no caller since D22.
- Pairing gap: if the first `paired` is lost and the Paper sends something else before the resend lands, the phone's
  receive counter passes it and drops the resent `paired`; pair again.
- Reader: a first open is 10-16 s, mostly layout. A TXT book is one chapter, loaded and parsed on every open (a 1 MB TXT
  would take seconds unless split at import). The worker is one queue: a job a tap waits for must not sit behind a slow
  write or decode (the cover write of a first open still delays taps for 2-3 s).
- An article open when the device is reset shows "New" again (progress is written when the book is left).
- The Today board with scheduled refresh off shows the date and events as of the power-off.
- Not built: a `link` message with the page title and its own share target (D36), pictures in articles, an article count
  on the plain sleep screen, a weather line on the launcher (from the saved copy, `weather_model` `fromJson` +
  `nextHours`).
- Chess: faster search needs move generation without a board copy and a vector per node.
- Project Gutenberg's illustrated EPUB of Pride and Prejudice is 24 MB; `tools/fetch-books.sh` uses fixed book ids and
  the no-images builds (gutendex.com timed out).
