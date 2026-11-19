# Settings

System configuration and developer tools.

## Screens

| Screen | Content |
|---|---|
| Settings (list) | Wi-Fi, Display, Power, Reading, Home Assistant, Storage, Developer, About |
| Wi-Fi | current network + signal; "Scan" lists networks (name, signal bars, lock); tap one, enter the password on `ui::Keyboard`, connect, save. Saved networks with "Forget". |
| Display | "Clean screen now"; ghost limit (Stepper 4..16); settle delay (Segmented 300/600/1000 ms) |
| Power | "When unused for" (Segmented 1/5/15/30 min/Never: switch-off on battery, `power.autoOffMin`); "Then show" Home | Sleep screen (`power.idleShow`, apps/launcher.md "When unused", D38); scheduled refresh of the sleep screen while off (Segmented Off/1/2/4/8 h, POWER.md); "Today on the sleep screen" (Toggle, `power.sleepBoard`, POWER.md "Today board"); stay awake on USB; battery mV and percent |
| Reading | default font size (S/M/L/XL), margins (narrow/normal/wide), line spacing |
| Home Assistant | base URL (read-only here), token present yes/no, "Test connection" (GET `/api/`), last error |
| Storage | SD card present/size/free; `/data` used/free; "Clear caches" |
| Developer | Display lab (DISPLAY.md), log tail, heap/PSRAM free, refresh counters, reboot |
| About | version, build date and build id, board, MAC, "Update over Wi-Fi" (below), licences (fonts, icons, libraries) |

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

Settings > Wi-Fi: status row (network, signal, IP; "Off: turns on when an app needs it"), "Join a network" (**every** network in range,
one row per SSID with the strongest access point, signal and channel, and Connected / Saved / Secured / Open; seven rows
to a page (`ui::PagedList`). One scan does not hear every access point, so the page scans three times in a row (1.5 s
apart) and adds what is new **at the end**: a row that is listed never moves under a finger, and a scan that heard nothing
new redraws nothing (`core/wifi.h` `ScanList`). The arrows in the title bar start over, in order of signal again. Until
2026-11-17 the page scanned once and kept the seven strongest: user, "It only does a scan once but it doesn't find/show
all". A known or open network joins at once, else a
password page with the on-screen keyboard as a bottom overlay, masked with a Show/Hide button, Enter joins and saves),
"Saved networks" (card entries marked, device entries can be forgotten by a tap), "Connect now". Settings > Home Assistant >
"Test connection": `GET <base URL>/api/` with the token; the page shows the HTTP status, the time and the start of the reply;
the token and the Authorization header are never shown or logged. Developer > "HTTP test": HTTPS GET of a public page.

## Config schema

`/data/config.json` (no secrets; all keys optional; defaults in `core/config.cpp`):

```json
{
  "display":  { "ghostLimit": 8.0, "settleMs": 600, "idleCleanMs": 2000, "pageCleanEvery": 10 },
  "power":    { "autoOffMin": 15, "autoOffMinReader": 30, "idleShow": "sleepScreen" },
  "net":      { "lingerSec": 20, "timezone": "CET-1CEST,M3.5.0,M10.5.0/3" },
  "reader":   { "fontSize": "M", "margins": "normal", "lineSpacing": 1.35 },
  "ha":       { "baseUrl": "http://homeassistant.local:8123" },
  "launcher": { "resumeLastApp": true },
  "phone":    { "clipboard": true, "sound": false }
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
`/data/companion.json` (secret: never shown, logged or readable over USB) and sends `paired`. Paired: the phone name, the switch
"Share the clipboard" (`phone.clipboard`, default on; DECISIONS.md D23: a copy on the Paper goes to the phone, what the phone shares
arrives before a paste), the switch "Sound for new messages" (`phone.sound`, default off: a chime on the buzzer when a read of the
relay brings new messages, not from 22:00 to 07:00; switching it on plays it once; apps/inbox.md "Sound") and "Unpair"
(with a confirmation; sends `unpair`, best effort, and deletes the pairing and the saved notifications). UI scripts `phone-pair`, `inbox-sound`.

## Files over Wi-Fi (Settings > Storage, P7-4, D30)

The page joins Wi-Fi, starts the file server (`core/file_server` on `hal::HttpServer`) and shows the address
(`http://192.168.1.69`, `:8080` in the simulator) and a new six-digit code. Mac: Finder > Go > Connect to Server, the address,
name `paper` (any), password the code. Phone or any browser: open the address; the page lists the books folder, uploads with
progress, opens folders, makes a folder (New folder), downloads a file (Download: saved under its own name, `?download` sends
`Content-Disposition: attachment`), and renames, moves (to a folder by its path, empty for the top) and deletes any file or
folder. Wi-Fi power save is off while the page serves (downloads 250-380 KB/s instead of ~30 KB/s; uploads stay ~100 KB/s, the
card's write speed) and on again when it stops. Checked in headless Chrome (Playwright) against the simulator: every action, at phone width. Below, what arrived ("Received Alice.epub", "3 files, 4.1 MB received", "Receiving x, 1.2 MB" during an
upload). Stop (or back) ends it; ten minutes without a request end it too ("Stopped after 10 minutes without use", Start again).
Golden `files-storage`, `files-wifi` (UI script `files-wifi`, a fake Wi-Fi network on its test card).

## Update over Wi-Fi (task P7-3, DECISIONS.md D37)

Settings > About > Update over Wi-Fi (`UpdatePage`). While the page is open the Paper joins Wi-Fi and listens on port 8266
(`update::kPort`, device and simulator) for a firmware package from the Mac:

```bash
make ota-key   # once: the signing key (~/.config/paperos/ota-signing.key) and its public half in src/core/update_key.h
make flash     # once after that, over USB: the firmware on the device must hold the public key
make ota       # from then on: builds, signs and sends (HOST=192.168.1.23 names the device)
```

- The page shows the address with the port, "On the Mac: make ota", what arrives ("Receiving build 3fa9c1d2e4b7", "1.2 MB of 2.5 MB"),
  the reason when a package is refused, and under it the build that runs and its slot. After a good image: "Build ...
  received. Restarting...", then the restart (2.5 s later, so the sender has its answer).
- It stops listening when the page is left, or after five minutes without a request. A firmware built before a key
  existed says "This firmware has no update key" and does not listen.
- `tools/ota.py send` takes the device's address from `--host` (`make ota HOST=...`), `$OTA_HOST`, a device on USB
  (`device.py status`), the last update (`out/ota/last-host`), or its usual address (`tools/ota.py host ADDRESS` saves
  it in `~/.config/paperos/ota-host`; the owner gave the device a reservation in the router on 2026-11-18, so it is
  saved on this Mac), and checks it with `GET /` (the page answers
  `{"paperos":"update","build":...}`). It never searches the network: that set a router against the Mac once (D37).
- Requests (`update::Server`): `GET /` (who this is), `POST /update` (the package; 200, or 400 / 403 / 409 / 413 / 500
  with the reason as text). The package format is in `src/core/update.h`.
- The new firmware is kept once it has run 15 s; before that a reset brings the old one back, and the page then says
  "The last update did not start: this is the firmware from before."
- Tests: `tests/unit/update_test.cpp` (the header and its signature, every refusal, a whole image), UI script `update`
  (golden `update-page`; `settings-about` has the row), and by hand against the simulator: open the page with
  `--remote`, then `uv run --script tools/ota.py send --sim` (the image lands in `/data/ota/image.bin`, the simulator
  exits as its restart).

