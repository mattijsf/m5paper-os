# Decisions

Each entry: what was decided, why, and what was rejected. Do not reopen a decision without new
facts (a measurement, a failed task). To change one, add a new entry that supersedes it.

## D1. PlatformIO with two environments: `sim` (native) and `papers3` (Arduino on pioarduino)

- **Why**: one tool builds both targets and manages the ESP32 toolchain and library versions.
  The Arduino framework gives working Wi-Fi, TLS, HTTP, SD and LittleFS with little code, which
  matters more than the last bit of control. pioarduino `55.03.312-1` provides Arduino core 3.x
  on ESP-IDF 5.x; the official `espressif32` platform is stuck on core 2.x.
- **Verified**: both environments build; firmware runs on the device (2026-11-15).
- **Rejected**: plain ESP-IDF (more control over power management, but much more glue code and
  a second build system for the simulator); Arduino IDE (no native target).

## D2. M5GFX + M5Unified as the hardware layer

- **Why**: they ship a tuned driver for this exact panel (4 waveform modes, background update
  task), touch, RTC, power-off and touch-wake light sleep for the PaperS3, maintained by the
  board vendor. M5GFX sprites and fonts also compile natively, so the same drawing code runs in
  the simulator.
- **Rejected**: epdiy (vendor docs mention it, but then touch, power and the simulator story are
  ours to build); writing a panel driver (months, risk of panel damage).

## D3. Own small UI kit instead of LVGL

- **Why**: LVGL assumes a fast display: animations, scrolling, frequent partial redraws. On
  e-ink each of those must be disabled or worked around, and refresh-mode control per region
  needs hooks into its flush path anyway. Our needs are a dozen static widgets and paginated
  lists; a retained widget tree with explicit `invalidate(hint)` is a few thousand lines and
  gives exact control over what reaches the panel.
- **Cost accepted**: we write layout and widgets ourselves (`UI.md`).
- **Rejected**: LVGL 9 (see above); immediate-mode UI (cannot know what changed without a diff
  of everything, and state handling gets messy).

## D4. Own simulator front end on SDL2, not M5GFX's `Panel_sdl`

- **Why**: we need things `Panel_sdl` does not do: headless PNG output without a window, an
  e-ink emulation with a refresh log, scripted input, fake time. Shared code draws into an
  `LGFX_Sprite`; the simulator only has to show that buffer.
- **Verified**: `program --headless --shot` works (2026-11-15).

## D5. Full-screen 8-bit grayscale canvas + diff against a shadow frame

- **Why**: 8 bits so anti-aliased glyphs blend correctly (4-bit palette sprites do not blend);
  a diff against the last pushed frame finds the minimal changed rectangles regardless of how
  sloppy the widgets invalidate, and lets the manager choose modes per region. 518 KB + 259 KB of
  8 MB PSRAM is affordable (6.5 MB free measured after the canvas).
- **Rejected**: drawing straight to `M5.Display` (no diff, no screenshots, device-only path).

## D6. "Fast then settle" as the default refresh strategy

- **Why**: measured on the device, Text mode takes 0.9-1.4 s and Fast 0.27-0.5 s. Showing a
  black/white Fast pass first and restoring grays with a Text pass once input stops gives quick
  response and clean text without flashing. Ghosting is bounded by a budget that triggers a
  Quality clean during navigation. Details: `DISPLAY.md`.
- **To revisit** in task P7-1 with custom waveforms (a shorter Text LUT may make the Fast pass
  unnecessary for page turns).

## D7. Light sleep is the idle state; power-off is the long-idle state; no deep sleep

- **Why**: touch (GPIO 48) is not an RTC pin, so it can wake light sleep only (confirmed in
  M5Unified source). Power-off through the PMS150G draws ~9 uA and wakes by button or RTC alarm,
  which is strictly better than deep sleep for long idle. E-ink keeps the image either way.
- Details and open measurements: `POWER.md`.

## D8. WhatsApp via phone notifications in Home Assistant, not a WhatsApp client

- **Why**: WhatsApp has no API for reading personal messages. The alternatives are unofficial
  WhatsApp Web clients (Baileys, whatsapp-web.js, WAHA) running on a server: against WhatsApp's
  terms (account ban risk), they break when WhatsApp changes its protocol, and they need a
  linked-device session to be kept alive. The user already runs Home Assistant with the Android
  companion app on the phones; its notification sensors deliver exactly "recent unread messages"
  with no new service and no terms risk.
- **Limits accepted**: only messages that produced a notification (not muted chats), text only,
  no media, no history beyond 25 items.
- **Fallback** if the user finds this too limited: a Baileys-based gateway (Phase 8), after
  asking the user.

## D9. Mail via the same notification feed first; full bodies later through a gateway

- **Why**: the user's address is Proton Mail, which has no IMAP without Proton Mail Bridge, so
  the device cannot fetch mail directly. Notifications give sender + subject today with zero
  infrastructure. Full text needs a home-network service (Bridge + IMAP to JSON): Phase 8, and
  only after the user confirms provider and host.
- **Open question for the user**: is sender + subject enough? Which mail provider(s)?

## D10. Home Assistant over REST, not WebSocket

- **Why**: the device is asleep most of the time; a persistent WebSocket would keep Wi-Fi up
  (150 mA). REST request/response fits "wake, fetch, sleep". Many states are read in one
  `POST /api/template` call.
- **Cost accepted**: no live push; the Home app polls every 30 s while in active use.

## D11. Apps are compiled-in C++ classes with static registration

- **Why**: simplest thing that makes adding an app a one-folder change. No scripting runtime,
  no dynamic loading, no extra RAM. `POS_REGISTER_APP` + a template folder + `tools/new-app.sh`.
- **Rejected**: Lua/JS/MicroPython apps (interpreter size, RAM, slow text layout, second API
  surface to document); loadable ELF modules (complex, fragile).

## D12. Fonts and icons as baked LVGL-format bitmap fonts

- **Why**: `lv_font_conv` (Node, already installed) produces compact 4-bit anti-aliased fonts
  with kerning; M5GFX loads the format natively; Lucide ships as an icon font, so icons use the
  same pipeline and draw as glyphs. No TTF rasteriser on the device.
- **Fonts**: Inter (UI), Literata (reading): both SIL OFL. **Icons**: Lucide (ISC), the same
  set the user's Home Assistant dashboard uses.
- **Rejected**: FreeType on device (RAM, speed); VLW fonts (no kerning, bigger); bitmap icons
  as PNG (one decode per draw).

## D13. EPUB parsed on the device, cached on the SD card

- **Why**: books must work with no server and no Wi-Fi. 8 MB PSRAM is plenty for zip inflate
  and one chapter. A one-time index per book (plain-text chapters + page offsets) makes later
  opens instant and page turns cheap.
- **Rejected**: converting books on a computer first (extra step for every book).

## D14. Portrait 540x960 only

- **Why**: reading and lists fit portrait; one layout halves the UI work. Rotation can be added
  later for the reader (landscape two-column is not planned).

## D15. Secrets in `/data/secrets.json`, provisioned over USB serial

- **Why**: a 180-character token cannot be typed on e-ink. Serial provisioning from a
  gitignored `config.local.json` is one command. Flash encryption is not enabled (it makes
  development and recovery harder); the trade-off is stated in `BACKEND.md`.
