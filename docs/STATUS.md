# Status

Living file. Update it at the end of every task (see ROADMAP.md "How to work a task").

## Now

- **Phase 0 is done** (2026-11-15). The repository holds: build setup for simulator and device,
  a spike that draws one mock home screen from shared code on both, and the full plan/docs.
- **P1-1 is done** (2026-11-15): HAL interfaces (`src/hal`), `POS_LOG`, `pos::fs`, `pos::Os`.
  Both `main.cpp` files build a `hal::Platform` and call `Os`; HAL classes are thin stubs.
- **Next task: P1-2** (device HAL: display, touch, clock, storage) in `ROADMAP.md`.
- The device runs the **spike firmware**: one static mock launcher. It is not interactive: a
  tap only draws a black dot. There are no apps or settings screens yet (the owner tried to open
  Settings on 2026-11-15; that arrives with tasks P2-5 and P2-6).
- The factory firmware is saved in `backup/factory-16MB.bin` (restore command in HARDWARE.md).

## What works today

| Thing | Command | Result |
|---|---|---|
| Simulator build | `make sim` | OK (first build ~3 min, then seconds) |
| Headless screenshot | `make shot` | `out/shot.png`, 540x960, 16 grays |
| Simulator window | `make run` | click draws a dot, S saves a screenshot |
| Firmware build | `make fw` | OK, ~665 KB (12.7% of the 5 MB slot) |
| Flash | `make flash` | OK over `/dev/cu.usbmodem101`, ~18 s |
| Serial heartbeat | `make monitor` | `[beat]` and `[sd]` lines every 5 s (the refresh benchmark is gone since P1-1) |
| Open books for the simulator | `make books` | 5 EPUB + 1 TXT in `sim-data/sd/books/` (not in git) |

## Measurements and checks on the device (2026-11-15, spike firmware)

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

## Decisions taken with the owner (2026-11-15)

| Topic | Answer | Recorded in |
|---|---|---|
| WhatsApp | Not phone notifications: a Home Assistant plugin reads and stores messages, the device reads the result | DECISIONS.md D16, BACKEND.md |
| Mail | Proton Mail, through Home Assistant if possible | D17 |
| Home Assistant token | The owner put it on the SD card: `/sd/paperos_config`, `HA_TOKEN=` | D19, apps/settings.md |
| Books | SD card is in, empty: find open books ourselves | `tools/fetch-books.sh`, task P4-0 pushes them to the card |

## Not verified yet (do not assume)

- **What the panel shows**: the user saw the mock launcher well enough to aim at the Settings
  tile, but nobody has confirmed the gray ramp order, sharpness, ghosting, or that the black
  dot appears under the finger. Ask once, or have the user send a photo.
- Light-sleep current, touch wake from light sleep, power-off and RTC wake (task P3-1).
- Whether a side-button click is readable by a running program (task P3-1).
- Home Assistant: entity ids come from the dashboard repo, not from the live instance; nothing
  has been requested from the live instance yet (tasks P5-1, P5-2).
- Which Home Assistant user the token belongs to (should be a dedicated non-admin user, D18).
- WhatsApp add-on: not installed; event fields for groups and the effect on phone
  notifications are unknown (task P6-1). `ha/paperos_inbox.yaml` is an untested draft.
- Proton Mail Bridge on Home Assistant OS: no confirmed add-on that exposes IMAP (task P6-2).

## Pending device checks

(none: P1-1 device check was run, see Log)

## Open questions for the user

1. **WhatsApp add-on risk**: it is an unofficial client linked to the personal account; its own
   documentation warns of possible bans. PaperOS only reads. To be confirmed once more at
   install time (task P6-1). The notification-sensor route stays available as fallback.
2. **Wi-Fi**: add `WIFI_SSID=` and `WIFI_PASSWORD=` to `paperos_config` on the card? It lets
   the device go online from task P3-2, long before the on-screen keyboard exists.
3. **Home Assistant user and URL**: is the token from a dedicated non-admin user? Add
   `HA_URL=http://<LAN address>:8123` to `paperos_config`? (task P5-1)
4. **Proton**: paid plan (needed for Bridge)? OK to run Bridge as a Home Assistant add-on?
   (task P6-2)

None of these block Phase 1, Phase 2, or the reader.

## Notes for later tasks

- LovyanGFX picks the colour format from the C++ type of the colour argument. The spike first
  passed `uint8_t` grays and got RGB332 colours (wrong gray ramp). `pos::Color` is `uint32_t`
  for this reason. Keep it.
- `partitions.csv`: nvs must end before 0xE000 (otadata) and app0 must start at 0x10000.
- `pio pkg exec -p tool-esptoolpy -- esptool` fails ("Permission denied"); call
  `~/.platformio/penv/bin/esptool` directly.
- Reading the device log from a script: open `/dev/cu.usbmodem101` at 115200 with pyserial
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
