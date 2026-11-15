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

## Config schema

`/data/config.json` (all keys optional; defaults in `core/config.cpp`):

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

`/data/secrets.json` (never in git, never logged):

```json
{
  "wifi": [ { "ssid": "MyNetwork", "password": "..." } ],
  "ha":   { "token": "<long-lived access token>" }
}
```

## Provisioning secrets

The Home Assistant token is ~180 characters: not something to type on an e-ink keyboard.

1. **USB serial (preferred)**: `tools/provision.py --port /dev/cu.usbmodem101` (task P3-3)
   reads `config.local.json` from the repo root (gitignored, same shape as `secrets.json` plus
   optional config keys) and sends it over serial as `>provision <base64>`; the device writes
   `/data/secrets.json` and merges the config keys.
2. **SD card**: put `secrets.json` in `/sd/paperos/`; at boot it is moved into `/data` and
   deleted from the card.
3. Wi-Fi networks can also be added on the device (Wi-Fi screen).

The simulator reads `sim-data/data/secrets.json` (gitignored).
