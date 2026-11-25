# Launcher

The home screen. Not a registered app: it is the root of navigation and always exists.

## Layout (540x960)

```
StatusBar (56)        12:34                    wifi  battery 87%
Header (144)          Sunday 15 November       <- title token
                      "3 unread - Reading: Dune, 42%"   <- caption, optional summary line
App grid              2 columns x 3 rows of ui::Tile (246 x 196, gap 24, margin 24)
                      icon 64 px in a gray(13) well, label below, optional Badge top-right
Pager (only if more than 6 apps)
```

The Phase 0 mock (`make shot`) shows the proportions; replace letters with icons.

## As built (P2-5)

`src/apps/launcher/launcher.{h,cpp}`: the first screen of the Shell (no title bar), header with the
date ("Sunday 15 November", or "PaperOS" while the RTC is unset), a 2 x 3 grid of `ui::Tile` (each
234 x 196: the 246 above is a typo; margins 24, gap 24) inside a `ui::Pager` whose bar is hidden when
there are at most six apps. Unused cells of the last row are blank tiles (no draw, no touch). Tiles
come from the registry, a tap calls `Shell::requestOpen`. Badges are read from
`/data/cache/<id>/badge` in `onShow()`. The quick panel is built (P7-6, below); resume-last-app is built (docs/POWER.md "Resume after power-off"). The summary line is built: "3 unread - Reading: <title>, 42%", read in `onShow()` from the apps' `/data/cache/<id>/badge` files and the reader's store (`/data/apps/read.json`, keys `last.title`, `last.percent`); parts that are missing are left out, nothing at all when both are; no network I/O. Goldens `launcher` (without data) and `launcher-summary` (with). Placeholder apps (`src/apps/placeholders.cpp`: Read, Home, WhatsApp,
Mail, Settings) show an `EmptyState` "Coming soon" until the real apps replace them. UI test:
`tests/ui/launcher.script` (open an app, back, long press on the back arrow).

Since the word game (2026-11-16) there are seven apps: the pager bar shows ("1 / 2") and the word game is alone on page 2 (golden
`wordgame-launcher-page2`).

## Behaviour

- Tiles come from the app registry sorted by `order`. Tap opens the app (`Hint::Screen`).
- Badges: an app may expose `int badge()` in its registration (unread count). The launcher
  reads the cached value from `/data/cache/<id>/badge`; it never triggers network I/O itself. The Read tile's badge
  counts what there is to read later, links that wait and articles nobody opened (apps/reader.md "Articles", P7-18);
  the summary line's "N unread" leaves it out (`cachedUnread()`: messages only).
- Swipe down from the status bar opens the quick panel (`Sheet`): Wi-Fi on/off state, battery
  detail, "Sleep now" (power off), "Clean screen" (full Quality refresh).
- On boot, if `config.launcher.resumeLastApp` is true (default) and the device was powered off
  from inside an app, the launcher opens that app directly.
- Agenda line (D28): under the date, what the phone's agenda has on now or next (`agenda::nextLine`, apps/agenda.md); hidden when
  there is nothing; from the saved agenda only. Golden `agenda-launcher-line`.
- The status bar clock updates once per minute with `Hint::Content` on its own rect only.

## When unused (P7-19, D38; replaces the idle return of P7-13, D33)

`Shell::idleCheck()`, one timer for the whole Shell (it rechecks at least once a minute). Nobody touched the device for
`power.autoOffMin` minutes (Settings > Power "When unused for": 1 / 5 / 15 / 30 min / Never; while the reader is open
`autoOffMinReader`, 30):

- **"Then show: Sleep screen"** (`power.idleShow`, the default): the open app is left (state saved) and the sleep screen
  goes on the panel (`Shell::showSleepScreen`), footer "Tap to start". The device stays on (on battery the PowerPolicy
  switches it off at the same moment: POWER.md "Auto-off"). A tap (any gesture that ends) leaves it (`Shell::wake`).
  **A tap on a part of it opens that app there**: the weather the Weather app on its blocks page (first place), an agenda row or day heading the
  Agenda's day view on that day, the book row (or the plain screen's cover) the reader, an unread count its app, the
  plain screen's agenda line the Agenda. The draw functions record the parts (`sleepTargets()`, `sleepTargetAt`); the
  Shell opens the app with `AppContext::link` ("date=2026-01-16", "page=1"), which an app reads in `onStart`
  (`ctx.linkValue(key)`) and may ignore. A tap anywhere else: the launcher, or the book or game that was open.
  Every 10 minutes it is drawn again if what it shows changed, and every `power.refreshHours` it reads the relay, the
  weather and links to read later first (the first time at the first look, 10 minutes on: the saved data may be hours
  old). Log: `[shell] not used for 15
  min: the sleep screen`, `[shell] sleep screen left by a tap: back to read`.
- **"Then show: Home"**: an app that is no book or game is left for the launcher (`[shell] mail not used for 15 min:
  back to the launcher`).
- **Idle** = the time since the last touch event (`Os::lastInputMs()`, which `Os::keepAwake()` also moves), but never
  longer than the app has been open: an app opened without a touch (the boot that resumes it, `>set screen`) gets its
  whole time.
- **Not while the screen on top says `Screen::staysOpen()`** (pairing, Files over Wi-Fi, Update over Wi-Fi, the
  Display lab), the on-screen keyboard shows, or a finger is down. Open overlays close first, each its own way
  (`ui::Root::closeOverlays()`).
- Books and games (`.stayOpen = true`: Read, Somdoku, the word game) are never left with "Home"; with "Sleep screen"
  they come back after the tap.
- For a device check: `tools/device.py set idle-min N` (minutes until the idle check acts, 0 = never; until the next
  boot, not saved; on battery it also sets the switch-off time).

Tests: unit cases "idle ..." (`app_test.cpp`: Home, the touch, 0 and the reader's time, stayOpen and staysOpen, a sheet,
the keyboard, the sleep screen and the tap, the network read every refreshHours), "auto-off ... resumes only a book or a
game" (`power_policy_test.cpp`); UI scripts `idle-home` (goldens `idle-before`, `idle-sleep-screen`, `idle-woken`,
`idle-read-sleep`, `idle-read-back`, `idle-power-setting`, `idle-home-launcher`) and `idle-board` (the Today board with
agenda and weather, "Tap to start"; taps on Friday's row, the weather, the WhatsApp count and an empty part: goldens
`idle-board-agenda`, `-weather`, `-launcher`), unit case "sleep screen: its parts are tap targets ...".

## Quick panel (P7-6, as built)

`core/quick_panel.cpp`, opened by `Shell::onGesture` for a **swipe down that started in the status bar** (y < 56) on every screen
that shows the status bar (not the reader's full-screen page, not under an open sheet). A `Sheet` with: Wi-Fi (cached state: the network
name or "Off until something needs it"), Battery (percent, charging or on battery, volts), and two buttons: **Clean screen** (one Quality
pass) and **Sleep now** (`Shell::sleepNow()`: save state, sleep screen, `powerOff(0)`; only the side button starts the device again).
No Wi-Fi switch: the net service switches it on and off itself. Goldens: `quick-panel*` (launcher.script, home.script).

## Files

`src/apps/launcher/launcher.cpp`, `src/ui/status_bar.cpp`, `src/ui/quick_panel.cpp`.
UI test: `tests/ui/launcher.script`.

## Fresh badges

The launcher shows what the apps cached, and until this day nothing read the phone's relay unless an app was opened.
Now: when the launcher comes up (boot, leaving an app) and a phone is paired and nobody read or tried the relay in the
last ten minutes of this boot (`inbox::relayCheckDue`, `kLauncherCheckSec`), it reads it in the background 1.5 s later
(`inbox::checkRelay`: the same read as the inbox's, on the worker; the store, both badges, the agenda and the clipboard
follow; a link shared for reading later is queued and counts in the Read tile's badge at once, its page is read
later: apps/reader.md "Articles", P7-18). A tap into an app before that cancels it; a failed try counts as a try (away from the known Wi-Fi a try is a
scan of several seconds). When the result is in and the launcher is still on top, the badges, the summary line and the
agenda line are set again: a tile whose count did not change is not drawn again; only the agenda line appearing or
going moves the tiles (one pass). Not done: no timer while the launcher just stays on (a device lying on USB does not
check again until an app was opened and left). On the device (boot): Wi-Fi joined in 1.3 s, the read took 1.2 s, the
log says `[relay] launcher check: read`, and the panel changed only by the Wi-Fi icon (nothing was unread); leaving
Settings a moment later started no second read. Goldens: `agenda-launcher-page2` (the agenda line and "4 unread" are
there before any app was opened), `inbox-badges-launcher`.

## Power button

The header is a row: the date, and in the right corner a `ui::IconButton` with the power icon (72 x 72, centre 480, 92).
A tap is `Shell::sleepNow()`, the same as "Sleep now" in the quick panel: state saved, the sleep screen on the panel
("Powered off", POWER.md "Sleep screen"), power off; no confirmation (one click on the side button starts the device
again, on the launcher). Why: the firmware cannot see the side button, and its double click leaves the last picture on
the panel, so off looked like on. The row is 72 high where the date took 24 + 36: the date sits 7 px higher, the gap
above the grid is 12 instead of 24 (`kGridGap`), and the grid is where it was. Test: `launcher: the power button
switches the device off through the sleep screen`; `tests/ui/sleep.script` taps it (`expect-off 0`, golden
`sleep-launcher-button` equal to `sleep-plain`).

## Twelve apps to a page

The grid is 3 columns x 4 rows of small tiles (`ui::Tile::Size::Small`: 153 x 160, the same 64 px icon and well as the
large tile, the label in the body size, the badge in the corner), 16 px apart. The nine apps fit one page with a row to
spare; the page bar comes back with the thirteenth app, and four rows end exactly where it starts, also with the agenda
line above them. Before: 2 x 3 large tiles, the seventh app on a second page. The Home app keeps the large tiles (they
hold a value line). Tile centres for scripts and injected taps: x 100 / 270 / 439, y 248 / 424 / 600 / 776 (28 lower
when the agenda line shows). Goldens: `launcher`, `launcher-summary`, `agenda-launcher`, `wordgame-launcher`,
`inbox-badges-launcher` and the ones that show the launcher behind something.

## No gray on the launcher

The small tiles have no gray well behind the icon. With the wells the launcher was a screen with flat gray areas, which
goes out as one Text pass (DISPLAY.md `screen-gray`), and the Text waveform draws in steps. Camera, Settings to the
launcher (`cam.py around tap 36 92`, one session):

| | on the panel |
|---|---|
| with wells, one Text pass (as it was) | three pulses of change over 366 ms: the flicker |
| with wells, Fast then settle (`>set gray-pass off`) | one pulse of 170 ms, then about a second later three more where the wells and edges are |
| **without wells** (Fast then settle by itself: nothing gray) | **one pulse of 144 ms**, then a ripple of about a sixth of it (the anti-aliased edges) |

Gray(13) read as 0.96-0.98 of white on the panel (DISPLAY.md): the wells were hardly there to the eye. The Home app's
large tiles lost theirs too: Lights to its Climate tab was one Text pass,
visible 397 ms; now a Fast pass of 195 ms and a settle the camera does not see.
`>set gray-pass off` (device only) switches the one-Text-pass rule off for a comparison.
