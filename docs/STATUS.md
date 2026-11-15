# Status

Living file. Update it at the end of every task (see ROADMAP.md "How to work a task").

## Now

- **Phase 0 is done** (2026-11-15). The repository holds: build setup for simulator and device,
  a spike that draws one mock home screen from shared code on both, and the full plan/docs.
- **Next task: P1-1** (HAL interfaces, logging, file paths) in `ROADMAP.md`.
- The device currently runs the spike firmware (mock launcher + touch dots + serial heartbeat).
  The factory firmware is saved in `backup/factory-16MB.bin` (restore command in HARDWARE.md).

## What works today

| Thing | Command | Result |
|---|---|---|
| Simulator build | `make sim` | OK (first build ~3 min, then seconds) |
| Headless screenshot | `make shot` | `out/shot.png`, 540x960, 16 grays |
| Simulator window | `make run` | click draws a dot, S saves a screenshot |
| Firmware build | `make fw` | OK, 618 KB (11.8% of the 5 MB slot) |
| Flash | `make flash` | OK over `/dev/cu.usbmodem101`, ~17 s |
| Serial heartbeat | `make monitor` | `[beat]` and `[bench]` lines every 5 s |

## Measurements from the device (2026-11-15, spike firmware)

- Board detected by M5Unified as id 19 (M5PaperS3); display reports 540x960 without extra rotation.
- PSRAM 8388608 bytes; 6564128 free after the 518 KB canvas and M5GFX's EPD buffers.
- Internal heap free after boot: 283 KB.
- Battery 3716-3750 mV = 52-56% while charging over USB.
- Refresh times (push + waveform): Quality full 1363 ms; Text full 1371 ms; Text row 540x96
  871 ms; Fast box 200x200 268 ms; Fast full 504 ms.
- Flash: 16 MB quad (manufacturer 0x46, device 0x4018).

## Not verified yet (do not assume)

- **What the screen actually looks like**: nobody has looked at the panel yet. The serial log
  proves the code ran and the waveforms were played, not that the image is right (orientation,
  gray ramp order, ghosting). First thing to confirm with the owner or a photo.
- Touch: no `[touch]` line has been seen yet (nobody tapped). Coordinates and orientation of
  touch relative to the display are unverified.
- Light-sleep current, touch wake from light sleep, power-off and RTC wake (task P3-1).
- Whether a side-button click is readable by a running program (task P3-1).
- SD card access (no card was checked) (task P1-2).
- Home Assistant: entity ids come from the dashboard repo, not from the live instance; the
  `/api/template` batch read and the inbox package are untested drafts (tasks P5-2, P6-1).
- The Android notification sensor entity ids and attributes on the user's phones (task P6-1).

## Pending device checks

(none yet: add here when a task is finished without a device attached)

## Open questions for the user

1. **Mail**: is "sender + subject" from phone notifications enough, or is full mail text
   wanted (needs a small home-server service, Phase 8)? Which mail app/provider?
2. **WhatsApp via phone notifications** (DECISIONS.md D8): acceptable? It shows only chats that
   notify (not muted ones), and needs the Home Assistant app's notification sensors enabled on
   the phone that has WhatsApp.
3. **Home Assistant token**: use the admin account's long-lived token (one fast request per
   refresh) or a separate non-admin user (safer if the device is lost, slower refresh)?
4. Is there a **microSD card** in the device? Books live on it. (Fallback: store books in the
   5.9 MB internal flash, room for a handful.)
5. Any **screen content preferences** for the Home app beyond the default in
   `docs/apps/home.md` (which lights/rooms matter most)?

None of these block Phase 1 to Phase 4.

## Notes for later tasks

- LovyanGFX picks the colour format from the C++ type of the colour argument. The spike first
  passed `uint8_t` grays and got RGB332 colours (wrong gray ramp). `pos::Color` is `uint32_t`
  for this reason. Keep it.
- `partitions.csv`: nvs must end before 0xE000 (otadata) and app0 must start at 0x10000.
- `pio pkg exec -p tool-esptoolpy -- esptool` fails ("Permission denied"); call
  `~/.platformio/penv/bin/esptool` directly.
- Running `pio run -e sim` and `pio run -e papers3` at the same time works but the first
  firmware build downloads ~1 GB of toolchains.
- The project has its own git repository (branch `main`, created 2026-11-15). Its parent
  folder is also a git repository (no commits): always run git from inside
  `m5paper-os` and check `git rev-parse --show-toplevel` if in doubt.

## Log

- 2026-11-15: Phase 0. Research (M5Stack docs, M5GFX/M5Unified 0.3.0 source), stack chosen,
  simulator + firmware spike built, factory flash backed up, spike flashed, timings measured,
  documentation set written.
