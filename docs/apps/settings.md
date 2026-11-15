# Settings

System configuration and developer tools.

## Screens

| Screen | Content |
|---|---|
| Settings (list) | Wi-Fi, Display, Power, Reading, Home Assistant, Storage, Developer, About |
| Wi-Fi | current network + signal; "Scan" lists networks (name, signal bars, lock); tap one, enter the password on `ui::Keyboard`, connect, save. Saved networks with "Forget". |
| Display | "Clean screen now"; ghost limit (Stepper 4..16); settle delay (Segmented 300/600/1000 ms) |
| Power | auto-off (Segmented 5/15/30/never-on-USB); battery mV and percent |
| Reading | default font size (S/M/L/XL), margins (narrow/normal/wide), line spacing |
| Home Assistant | base URL (read-only here), token present yes/no, "Test connection" (GET `/api/`), last error |
| Storage | SD card present/size/free; `/data` used/free; "Clear caches" |
| Developer | Display lab (DISPLAY.md), log tail, heap/PSRAM free, refresh counters, reboot |
| About | version, build date, board, MAC, licences (fonts, icons, libraries) |

## As built (P2-6)

`core/config.{h,cpp}` (typed structs `display power net reader ha launcher`, `load()` clamps ranges and
ignores wrong types, `save()` writes atomically), `core/secrets.{h,cpp}` (`present`, `keys`, `get`;
read-only), `core/timefmt` (time zone from `net.timezone`, used by the status bar and launcher),
`hal/system.h` (storage sizes, memory, restart, board, MAC: per-platform hooks), `Os::loadConfig()`
(called by the platform mains after the storage is mounted: loads config and secrets, applies the
display parameters and the time zone). `AppContext` gained `config` (read-only) and `secrets`; `os()`
is for built-in system apps only. Settings (`src/apps/settings/settings_app.cpp`): the list; **Display**
(Clean screen now = the one Quality refresh, ghost limit Stepper 4..16 and settle delay Segmented, both
applied to the DisplayManager at once and saved); **Power** (switch-off Segmented, battery); **Reading**
(font size, margins, line spacing: stored only, the reader comes in P4); **Home Assistant** (base URL,
token "present" / "not configured", key names; values never shown; Test connection is a stub);
**Storage** (SD and `/data` sizes, Clear caches); **Developer** (heap, PSRAM, refresh counters, log tail,
Reboot); **About** (version, build, board, MAC, licences); **Wi-Fi** shows a notice until P3. The
simulator reports fixed fake storage and memory values so screenshots are deterministic.
A unit test walks every Settings page with a secrets file holding recognisable values and checks that
none of them appears in any widget text or in the log.

## Config schema

`/data/config.json` (no secrets; all keys optional; defaults in `core/config.cpp`):

```json
{
  "display":  { "ghostLimit": 8.0, "settleMs": 600, "idleCleanMs": 2000 },
  "power":    { "autoOffMin": 15, "autoOffMinReader": 30 },
  "net":      { "lingerSec": 20, "timezone": "CET-1CEST,M3.5.0,M10.5.0/3" },
  "reader":   { "fontSize": "M", "margins": "normal", "lineSpacing": 1.35 },
  "ha":       { "baseUrl": "http://homeassistant.local:8123" },
  "launcher": { "resumeLastApp": true }
}
```

## Secrets and provisioning: `/sd/paperos_config`

Secrets live in one text file in the root of the SD card, `KEY=VALUE` per line, `#` comments:

```
# /sd/paperos_config
HA_TOKEN=<long-lived access token>         # present on the card in the device (2026-11-15)
HA_URL=http://<home assistant LAN ip>:8123 # optional, overrides config.json ha.baseUrl
WIFI_SSID=<network name>                   # optional: lets the device join Wi-Fi before the
WIFI_PASSWORD=<password>                   #   on-screen keyboard exists (task P3-7)
```

- The user created this file by putting the card in a computer. The device reads it at boot
  (`core/secrets`, task P3-3) and **never writes to it, logs its values, or shows them**; the
  Settings screens show "token present" and the key names only.
- Parsing: split at the first `=`, trim spaces and carriage returns, ignore blank lines,
  `#` lines and unknown keys. Missing file or key = "not configured", never a crash.
- Wi-Fi networks added on the device (Wi-Fi screen) are stored in `/data/wifi.json` (internal
  flash), also never logged. `WIFI_SSID`/`WIFI_PASSWORD` from the card are tried first.
- The simulator reads the same file from `sim-data/sd/paperos_config` (gitignored). Tests use
  fixtures and need no token.
- No SD card: the device still boots; Home and inbox apps show "No Home Assistant token:
  put paperos_config on the SD card".
