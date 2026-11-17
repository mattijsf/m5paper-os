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
  reads the cached value from `/data/cache/<id>/badge`; it never triggers network I/O itself.
- Swipe down from the status bar opens the quick panel (`Sheet`): Wi-Fi on/off state, battery
  detail, "Sleep now" (power off), "Clean screen" (full Quality refresh).
- On boot, if `config.launcher.resumeLastApp` is true (default) and the device was powered off
  from inside an app, the launcher opens that app directly.
- Agenda line (D28): under the date, what the phone's agenda has on now or next (`agenda::nextLine`, apps/agenda.md); hidden when
  there is nothing; from the saved agenda only. Golden `agenda-launcher-line`.
- The status bar clock updates once per minute with `Hint::Content` on its own rect only.

## Quick panel (P7-6, as built)

`core/quick_panel.cpp`, opened by `Shell::onGesture` for a **swipe down that started in the status bar** (y < 56) on every screen
that shows the status bar (not the reader's full-screen page, not under an open sheet). A `Sheet` with: Wi-Fi (cached state: the network
name or "Off until something needs it"), Battery (percent, charging or on battery, volts), and two buttons: **Clean screen** (one Quality
pass) and **Sleep now** (`Shell::sleepNow()`: save state, sleep screen, `powerOff(0)`; only the side button starts the device again).
No Wi-Fi switch: the net service switches it on and off itself. Goldens: `quick-panel*` (launcher.script, home.script).

## Files

`src/apps/launcher/launcher.cpp`, `src/ui/status_bar.cpp`, `src/ui/quick_panel.cpp`.
UI test: `tests/ui/launcher.script`.

## Fresh badges (2026-11-17)

The launcher shows what the apps cached, and until this day nothing read the phone's relay unless an app was opened.
Now: when the launcher comes up (boot, leaving an app) and a phone is paired and nobody read or tried the relay in the
last ten minutes of this boot (`inbox::relayCheckDue`, `kLauncherCheckSec`), it reads it in the background 1.5 s later
(`inbox::checkRelay`: the same read as the inbox's, on the worker; the store, both badges, the agenda and the clipboard
follow). A tap into an app before that cancels it; a failed try counts as a try (away from the known Wi-Fi a try is a
scan of several seconds). When the result is in and the launcher is still on top, the badges, the summary line and the
agenda line are set again: a tile whose count did not change is not drawn again; only the agenda line appearing or
going moves the tiles (one pass). Not done: no timer while the launcher just stays on (a device lying on USB does not
check again until an app was opened and left). On the device (boot): Wi-Fi joined in 1.3 s, the read took 1.2 s, the
log says `[relay] launcher check: read`, and the panel changed only by the Wi-Fi icon (nothing was unread); leaving
Settings a moment later started no second read. Goldens: `agenda-launcher-page2` (the agenda line and "4 unread" are
there before any app was opened), `inbox-badges-launcher`.

## Twelve apps to a page (2026-11-17)

The grid is 3 columns x 4 rows of small tiles (`ui::Tile::Size::Small`: 153 x 160, the same 64 px icon and well as the
large tile, the label in the body size, the badge in the corner), 16 px apart. The nine apps fit one page with a row to
spare; the page bar comes back with the thirteenth app, and four rows end exactly where it starts, also with the agenda
line above them. Before: 2 x 3 large tiles, the seventh app on a second page. The Home app keeps the large tiles (they
hold a value line). Tile centres for scripts and injected taps: x 100 / 270 / 439, y 248 / 424 / 600 / 776 (28 lower
when the agenda line shows). Goldens: `launcher`, `launcher-summary`, `agenda-launcher`, `wordgame-launcher`,
`inbox-badges-launcher` and the ones that show the launcher behind something.
