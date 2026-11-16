# Settings

System configuration and developer tools.

## Screens

| Screen | Content |
|---|---|
| Settings (list) | Wi-Fi, Display, Power, Reading, Home Assistant, Storage, Developer, About |
| Wi-Fi | current network + signal; "Scan" lists networks (name, signal bars, lock); tap one, enter the password on `ui::Keyboard`, connect, save. Saved networks with "Forget". |
| Display | "Clean screen now"; ghost limit (Stepper 4..16); settle delay (Segmented 300/600/1000 ms) |
| Power | auto-off (Segmented 5/15/30/never-on-USB); scheduled inbox refresh while off (Segmented Off/1/2/4/8 h, POWER.md); stay awake on USB; battery mV and percent |
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

## As built: Wi-Fi screens and Home Assistant test (P3-7)

Settings > Wi-Fi: status row (network, signal, IP; "Off: turns on when an app needs it"), "Join a network" (scan, one row
per SSID with the strongest access point, signal and channel, Secured/Open; a known or open network joins at once, else a
password page with the on-screen keyboard as a bottom overlay, masked with a Show/Hide button, Enter joins and saves),
"Saved networks" (card entries marked, device entries can be forgotten by a tap), "Connect now". Settings > Home Assistant >
"Test connection": `GET <base URL>/api/` with the token; the page shows the HTTP status, the time and the start of the reply;
the token and the Authorization header are never shown or logged. Developer > "HTTP test": HTTPS GET of a public page.

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

`inbox.whatsapp.favorites` (D21) was removed with the direct WhatsApp send (D22, 2026-11-16): the key is not read, and the next
save of the file drops it. Replies go only to chats with a live notification on the phone (apps/inbox.md "Replying").

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
  Settings screens show "token present" and the key names only. `>status` (and `tools/device.py status`) prints key names and
  value lengths, Wi-Fi state, heap, and a count of how often the first 8 characters of any secret value occur in the last
  50 log lines (must be 0).
- Parsing: split at the first `=`, trim spaces and carriage returns, ignore blank lines,
  `#` lines and unknown keys. Missing file or key = "not configured", never a crash.
- **Several Wi-Fi networks**: `WIFI_SSID` + `WIFI_PASSWORD` for the first
  network, `WIFI_SSID_2` + `WIFI_PASSWORD_2` ... up to `_8` for more. A network without a password key (or an
  empty one) is open. The value is everything after the **first** `=` of the line, trimmed, so an SSID may
  contain `/`, `.`, spaces and `=` (the real one on the card has a `/` and a `.`).
- Networks added on the device (Wi-Fi screen) are stored in `/data/wifi.json` (internal flash, never logged), with
  the network that worked last and, per network, the BSSID and channel of the access point. **Known networks =
  card entries + device entries; the card wins on duplicates** (same SSID).
- **Connecting** (`core/net`): the network that worked last first, straight to its stored BSSID/channel (the fast
  path, measured 1.2 s on the device); if that fails or there is none, one scan and a join of the known network
  with the strongest signal (measured 5.3 s with the scan); BSSID/channel are remembered after each success.
  Never a loop: one fast attempt, one scan attempt, then the failure goes to the caller (apps show "No Wi-Fi").
  The radio is **2.4 GHz only**: a network that exists only on 5 GHz is not found; the scan result (SSIDs, signal,
  channel, no passwords) is logged and shown ("No known network in range") and nothing is retried.
- The simulator reads the same file from `sim-data/sd/paperos_config` (gitignored). Tests use
  fixtures and need no token.
- No SD card: the device still boots; Home and inbox apps show "No Home Assistant token:
  put paperos_config on the SD card".

## Phone (Settings > Home Assistant > Phone)

Pairing with the companion Android app (DECISIONS.md D22, `docs/COMPANION.md` section 2). It sits under Home Assistant because the
Settings list is full (eight rows). Not paired: "Pair phone" makes new keys and a pairing id and shows a QR code
(`paperos1:<pid>:<public key>:<HA_URL>`, `ui/qr.h`), then polls `sensor.paperos_relay_pairing` every 3 s for at most 15 minutes. When
the phone's hello arrives: the phone name and a six-digit code to compare with the phone; "Codes match" saves the pairing to
`/data/companion.json` (secret: never shown, logged or readable over USB) and sends `paired`. Paired: the phone name and "Unpair"
(with a confirmation; sends `unpair`, best effort, and deletes the pairing and the saved notifications). UI script `phone-pair`.
