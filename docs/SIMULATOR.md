# Simulator and testing

The simulator is the same `core`, `ui` and `apps` code built for macOS with SDL2. It is the
default way to develop: build and screenshot take seconds, no device needed. The device is for
what a model cannot show: real refresh behaviour, touch feel, power.

## What exists now (Phase 0)

```bash
make sim     # build .pio/build/sim/program
make run     # window: mouse click = touch, S = screenshot to shot-N.png, Q or Esc = quit
make shot    # headless: writes out/shot.png and exits (no window, works over SSH)
make fw      # build device firmware
make flash   # build + flash (device on /dev/cu.usbmodem*)
make monitor # device serial log
make check   # shot + fw: must pass before any task is called done
```

`program --headless --shot <file.png>` renders one frame and exits. The PNG is 540x960
grayscale quantised to the panel's 16 levels. **UI work is verified by running
`make shot` and looking at `out/shot.png`.**

## Target (built in tasks P1-4, P1-5)

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
```

Exit code is non-zero if an `expect-*` fails or the app crashed.

### Golden screenshots

`tools/ui-test.sh` (task P1-5) runs every `tests/ui/*.script` headless into `out/ui/`, then
compares each PNG byte-for-byte with `tests/ui/golden/<name>.png` (rendering is deterministic:
fixed fonts, fake clock, fixtures).

```bash
make ui-test            # run all, list mismatches, exit 1 on any
make ui-accept          # copy out/ui/*.png over the goldens after looking at them
```

A mismatch is not automatically a bug: look at the new image; if it is the intended change,
accept it and commit the golden with the code.

### HTTP fixtures

`hal::Net::request` in fixture mode maps a request to a file:

```
tests/fixtures/http/<METHOD>_<host>_<path with / replaced by _>[_<query hash>].json
example: POST_ha.local_api_template.json, GET_ha.local_api_states_sensor.paperos_whatsapp_inbox.json
```

A `<name>.status` file next to it can hold a status code (to test errors). Unknown requests
return status 0 with error "no fixture" and are listed at exit so missing fixtures are obvious.
With `--fixtures none` the simulator uses the real network through libcurl and the real
`sim-data/sd/paperos_config` (not in git).

### E-ink emulation and the refresh log

Described in `DISPLAY.md`. The window and screenshots show the emulated panel (thresholded Fast
passes, ghost residue), not the raw canvas. `out/refresh.log` records every push.

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
- The simulator speaks the same protocol: `program --headless --remote` reads `>` commands on
  stdin and replies on stdout (`--name M5PaperS3` for an identical footer). Send `>shot` alone
  and wait for its reply: it runs after earlier touches were handled, so it answers after
  later one-line commands. Commands per line, replies end with `#ok` or `#err <reason>`.
- Whole UI scripts on the device: task P7-2.
- Things only the device can answer are listed per task as "Device check" in `ROADMAP.md`.
  If no device is attached, finish the task in the simulator and add the device check to
  `STATUS.md` under "Pending device checks". Never claim a device check passed without running it.
