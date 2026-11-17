# Simulator and testing

The simulator is the same `core`, `ui` and `apps` code built for macOS with SDL2. It is the
default way to develop: build and screenshot take seconds, no device needed. The device is for
what a model cannot show: real refresh behaviour, touch feel, power.

## What exists now (Phase 0)

```bash
make sim     # build .pio/build/sim/program
make run     # window: mouse = touch (drag = move), S screenshot to shot-N.png, G ghost overlay, L refresh log, Q/Esc quit
make shot    # headless: writes out/shot.png and exits (no window, works over SSH)
make fw      # build device firmware
make flash   # build + flash (device on /dev/cu.usbmodem*)
make monitor # device serial log
make check   # shot + fw: must pass before any task is called done
```

`program --headless --shot <file.png>` renders one frame and exits. The PNG is 540x960
grayscale quantised to the panel's 16 levels. **UI work is verified by running
`make shot` and looking at `out/shot.png`.**

## Command line, scripts, goldens (P1-4, P1-5)

Implemented: `--headless --script --out --data --time --scale --name --remote`; `--app NAME` opens a registered app (`--app read`), a UI test screen (`widgets1`, `widgets3`, ...) or the font
sheet (`fonts`); without it the launcher shows (a script file can start with a `# app: NAME` line, which
`tools/ui-test.sh` passes as `--app`). `--no-rtc` makes `now()` 0 like a device whose RTC was never set, until a time sync sets the clock (then the fake or host time; a
script line `# rtc: unset` passes it); `--fixtures` is ignored until P5. Script commands implemented: `wait`, `shot`,
`tap`, `long`, `swipe`, `back` (taps (36, 92), the back-arrow target), `expect-no-quality`,
`expect-max-pushed N` (sum of pushed rectangle areas since the last `shot`). `type TEXT` and `type-max N TEXT` (tap the keys of the visible
on-screen keyboard; with a limit every character key may push at most N pixels), `battery`, `wifi` wait for their features. `--time` takes `YYYY-MM-DDTHH:MM:SS` (UTC). In `--headless` runs the clock
is fake; `tools/ui-test.sh` runs every script with `--data out/ui-data` (empty, never `sim-data/`) and
`--time 2026-01-15T12:34:00` (UTC; the status bar shows 13:34 in the default zone CET). Every script gets its own empty data root
`out/ui-data/<script>/`, so config written by one script (Settings) never leaks into another. Worker jobs run in real time while the fake clock jumps; the script
runner sleeps 3 ms real time per 10 ms step while a job is pending so results arrive at a repeatable
simulated time (checked: 15 identical `make ui-test` runs). `make gallery` writes `out/gallery.html`.

### Command line

```
program [--headless] [--script FILE] [--out DIR] [--fixtures DIR] [--data DIR]
        [--time ISO8601] [--scale N] [--app NAME]
```

| Flag | Meaning |
|---|---|
| `--headless` | no window; requires `--script` or `--shot` |
| `--script FILE` | run a UI script (below), then exit (headless) or keep the window open |
| `--out DIR` | where `shot` commands and `refresh.log` are written (default `out/`) |
| `--fixtures DIR` | answer HTTP from files instead of the network (default in headless mode: `tests/fixtures/http`) |
| `--data DIR` | root for `/sd` and `/data` (default `sim-data/`; tests use a temp copy) |
| `--time T` | start the fake clock at T; in headless mode time only advances by `wait` |
| `--app NAME` | open this app directly instead of the launcher |

Window keys: `S` screenshot, `G` toggle ghost-residue overlay, `L` print refresh log tail,
`P` simulate the side button (power on/off), `B` cycle fake battery 100/50/15/5%, `W` toggle
"Wi-Fi unavailable", `Q` quit.

### UI scripts

Plain text, one command per line, `#` comments:

```
# tests/ui/launcher.script
wait 500              # advance time by ms and run the main loop
shot launcher         # writes out/launcher.png (from the emulated panel)
tap 140 300           # down + up at x,y
wait 800
shot reader-library
swipe 400 500 100 500 # from x1,y1 to x2,y2
long 30 90            # long press
back                  # tap the back arrow
type hello            # taps keys on the on-screen keyboard
battery 15            # set fake battery percent
wifi off              # make connections fail
expect-no-quality     # fail if the refresh log since the last 'shot' contains a Quality push
expect-max-pushed 20000   # fail if more than N pixels were pushed since the last 'shot'
restart               # save and close the app (as at power-off), build the surface again: a boot with the same data
timer-wake            # power-off, then the RTC alarm: the clock jumps and the device boots into the scheduled refresh (P7-5)
expect-off 7200       # the device switched itself off with a wake alarm in N seconds (0 = no alarm, -1 = it is on)
expect-state page=7/  # the top screen's debugState() contains the text (Screen::debugState(); the reader says page=N/M)
expect-shot-equal A B # two earlier shots are byte-identical (resume after restart, same page on the second visit)
expect-frame-equal A B # what the screen drew at two earlier shots is identical; the shots themselves may differ, because they
                      # show the emulated panel with its history (pixels outside a Fast pass's box keep the last Quality state)
page-through 200      # tap the right zone until the reader's last page (page=N/N); fails after 200 taps
```

Exit code is non-zero if an `expect-*` fails or the app crashed.

### Golden screenshots

`tools/ui-test.sh` (task P1-5) runs every `tests/ui/*.script` headless into `out/ui/`, then
compares each PNG byte-for-byte with `tests/ui/golden/<name>.png` (rendering is deterministic:
fixed fonts, fake clock, fixtures). The simulator is started through `run_sim` (`tools/sim-run.sh`, also used by
`tools/books-check.sh`): a run that hangs is killed after `SIM_LIMIT` seconds (120; exit code 142) and shows as a failed
script instead of blocking `make check`.

```bash
make ui-test            # run all, list mismatches, exit 1 on any
make ui-accept          # copy out/ui/*.png over the goldens after looking at them
```

A mismatch is not automatically a bug: look at the new image; if it is the intended change,
accept it and commit the golden with the code.

### Wi-Fi world and test data

The simulator's `hal::Net` is a small fake Wi-Fi world: HomeNet (two access points, channels 6 and 11), Neighbour, CafeFree
(open); script commands `wifi off` / `wifi on` make no network reachable, `wifi crowd N` puts N more networks in range
("Flat 01".., weaker than the four) of which the first scan hears every second one (the scan page's paging and
rescans, `tests/ui/wifi-scan.script`). A script can come with preset data:
`tests/ui/data/<script name>/` is copied into that script's data root before the run (`sd/paperos_config` with fake values
for the Settings and Wi-Fi scripts). HTTP is answered from `--fixtures DIR` (default `tests/fixtures/http`), or from the real
network through libcurl with `--fixtures none`; missing fixtures are listed on stderr at exit.

### HTTP fixtures

`hal::Net::request` in fixture mode maps a request to a file:

```
tests/fixtures/http/<METHOD>_<host>_<path with / replaced by _>[_<query hash>].json
example: POST_ha.local_api_template.json, GET_ha.local_api_states_sensor.paperos_whatsapp_inbox.json
```

A request with a body is first looked up as `<name>_b<FNV-1a of the body, 8 hex>.json`, then under the plain name: two POSTs to
one URL that differ only in the body (the weather app's `weather.get_forecasts`, daily and hourly) get their own files, every other
fixture keeps its plain name (`fixtureFileName(method, url, body)` in `core/http_fixture`).
A `<name>.status` file next to it can hold a status code (to test errors). Unknown requests
return status 0 with error "no fixture" and are listed at exit so missing fixtures are obvious.
With `--fixtures none` the simulator uses the real network through libcurl and the real
`sim-data/sd/paperos_config` (not in git).

### E-ink emulation and the refresh log

Described in `DISPLAY.md`. The window and screenshots show the emulated panel (Fast passes in black and white by the panel
driver's ordered dither, ghost residue), not the raw canvas. A screenshot taken before the settle pass therefore shows grays
as dots. `out/refresh.log` records every push.

### Unit tests

Pure logic (EPUB zip reading, XHTML to paragraphs, pagination, JSON mapping, ghost accounting)
gets host unit tests under `tests/unit/` (`make test`: `pio test -e test --without-testing`, then the
built program runs directly; PlatformIO's own doctest runner reports 0 cases and "SKIPPED" for our
custom `main`, so its summary is ignored). `env:test` compiles `src/core`, `src/hal`, `src/ui` and
`src/apps` only (no `platform/`); `tests/unit/main.cpp` holds the runner and a silent log sink
(`POS_TEST_LOG=1` prints logs). doctest comes from the PlatformIO registry. Keep them fast: all of
`make test` under 10 s.

## What `make check` means

Since P1-3: `shot`, `test`, `fw`. From task P1-5 on it is: `sim` build, `test`, `ui-test`, `fw` build.
A task is done only when `make check` passes.

## Device testing

- `make flash && make monitor`. Logs use the same `POS_LOG` lines as the simulator.
- Remote control over USB (task P1-8): `tools/device.py shot out/device.png` dumps the frame
  the firmware drew (4-bit pixels over serial, converted to PNG), `tools/device.py tap 140 300`
  injects a touch. With these, device checks need neither eyes nor fingers. The dump is the
  frame buffer, not a photo: it cannot show ghosting or a half-played waveform.
  **Log lines wait while a reply leaves** (`RemoteHost::replyLeaving()`, 2026-11-16): a log line printed between two pieces of a
  frame dump (the heartbeat every 5 s, a push) lands inside the pixels, the host never sees `#ok` and reports "no reply to '>shot'".
  Seen twice on 2026-11-16; in the failing case the heartbeat of that second shows the dump's 518 KB allocated. The device now holds
  its serial log from a reply's first byte to its `#ok` / `#err` line and prints it then (`>log` has every line at once); 80 dumps
  in a row across six heartbeats came back whole.
  Replies are queued and sent between main-loop passes (`RemoteHost::writable()`, 2026-11-16): a dump takes ~0.5 s and no
  longer stops the UI (it held the loop ~1.07 s).
- The simulator speaks the same protocol: `program --headless --remote` reads `>` commands on
  stdin and replies on stdout (`--name M5PaperS3` for an identical footer). Send `>shot` alone
  and wait for its reply: it runs after earlier touches were handled, so it answers after
  later one-line commands. Commands per line, replies end with `#ok` or `#err <reason>`.
- Files over the same link (P4-0): `>put <path> <size> <crc32hex>` (device answers `#put go`, then raw bytes,
  `#ack <total>` every 4096 bytes, `#ok` after the CRC matched and the `.part` file was renamed; a stalled upload
  is dropped after 8 s), `>ls <dir>` (`#ls <bytes>` then `name<TAB>size` lines, `-` for folders), `>rm <path>`.
  Only `/sd/books/...` and `/sd/paperos/...` are allowed, never `paperos_config`, never `..`; the two root
  folders cannot be removed. `tools/push.py [--to /sd/books/] FILE...` skips files already there with the same size
  (`--force` to resend), `--sim --data DIR` talks to a simulator instead of the device. `make books` = fetch + push.
  `tools/device.py ls DIR` / `rm PATH`. Right after a flash the port needs a few seconds before it answers.
- **Whole UI scripts on the device (P7-2)**: `tools/device.py script tests/ui/NAME.script` (add `--sim` to run the same thing against the simulator's
  remote mode) opens the script's `# app:` screen with `>set screen`, plays `wait`, `tap`, `long`, `swipe`, `back`, `shot`, `expect-no-quality`,
  `expect-max-pushed` (from `>refresh`), `expect-state`, `page-through`, `page-until` (from the new `>state` = `Screen::debugState()`) and
  `expect-shot-equal` / `expect-frame-equal` (the same on the device: its shot is the drawn frame), saves every `shot` to `out/device-ui/` and compares it with the golden: IDENTICAL, identical below the status bar (the top 56
  rows hold the live clock and battery), or DIFFERENT with a pixel count.
  **The tool never hangs**: a serial write times out after 5 s, an unanswered command is sent again with a line on stderr, and the whole
  run ends with exit code 3 after `--limit` seconds (default 90; a script: its waits + 20 s per step + 60). `-v` prints every script step.
  `tools/device.py scripts` lists what can run:
  - **compare** (no card data, no fixtures): `textblock`, `widgets1`, `widgets2`, `widgets2list`, `widgets2rows`, `widgets3`; also `launcher` and
    `display-lab`, which cannot match on a real device (live date line, counters and summary line; with a book open the Read tile goes
    straight to the book, so `launcher` goes off its path). Device run 2026-11-15: the first five identical, `widgets3` 6 of 12 (STATUS.md).
  - **data** (runs, but the goldens come from preset card data and HTTP fixtures the device does not have): `home`, `inbox-empty`, `inbox-link`,
    `launcher-summary`, `library`, `reader-menu`, `settings`.
  - **cannot**: scripts with `type` / `type-max` (key positions are not exposed: `keyboard`, `wifi`, `textfield-menu`, `whatsapp-send`,
    `whatsapp-send-refused`, `whatsapp-send-missing`), `wifi on|off` (simulator only: `home-offline`,
    `inbox-error`, `inbox-mail`), `restart` (`epub`, `inbox`, `inbox-wifi`, `reader`), `power-off` (`sleep`, `sleep-reader`), `timer-wake` (`sched-refresh`; on the device: `poweroff N M`), and `fonts` (drawn by the Os).
  Time is real on the device: a shot right after a tap can catch the frame before or after the settle pass, so it may differ from the golden by a few
  hundred anti-aliased pixels (seen in `widgets3-stepper` and `-level` even against the simulator's remote mode).
- **Live view (2026-11-16)**: `make view` (`tools/view.py`) serves a page on `http://127.0.0.1:8137/` with the device's frame; a click is a
  tap, holding 600 ms a long press, a drag a swipe (pointer events, so a finger on a phone works too). One thread owns the link: it polls
  `>refresh` every 0.15 s and takes a `>shot` when the refresh log changed, after every input, and every 5 s; the page long-polls for the
  next frame. Measured on the device: click to new frame in the page ~760 ms (dump 400-550 ms), so at most ~2 frames per second.
  `ARGS=--sim` shows the simulator's remote mode instead. `ARGS=--lan` listens on every interface for a phone on the same network; the
  address then carries a random key (whoever opens the page sees the screen and can tap), without it only this computer can open the page
  (Host and Origin are checked). It stops by itself after 300 s without an open page (`--idle`), because it holds the serial port and the
  device does not sleep while a host holds it. Same limits as the frame dump: no ghosting, no flash.
  **One program per port**: `device.py`'s `Link` opens the port exclusively, so a second tool (while the viewer or another tool runs) ends at
  once with "in use by another program" instead of sharing replies with it.
- **Camera (P7-7)**: what the panel physically shows (ghosting, flash, refresh timing on the panel, gray levels) comes from a webcam:
  `make cam-shot`, `make cam-video`, `make cam-compare`, `tools/cam.py around tap X Y`. Details and limits: DISPLAY.md "Camera on the panel".
- Things only the device can answer are listed per task as "Device check" in `ROADMAP.md`.
  If no device is attached, finish the task in the simulator and add the device check to
  `STATUS.md` under "Pending device checks". Never claim a device check passed without running it.

## Real-time runs against the simulator

`tools/device.py --sim script FILE` runs a UI script against the simulator in real time (waits are real), so other programs can
talk to it meanwhile (Files over Wi-Fi: curl or a WebDAV client against `http://localhost:8080`). `--sim-data DIR` gives it
another data root, for example a card with a fake Wi-Fi network (`tests/ui/data/files-wifi`). Copy a fresh build to
`.pio/build/sim/program` with `rm` first: macOS kills a binary copied over the old one in place (exit 137).
