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

Since Vijfje (2026-11-16) there are seven apps: the pager bar shows ("1 / 2") and Vijfje is alone on page 2 (golden
`vijfje-launcher-page2`).

## Behaviour

- Tiles come from the app registry sorted by `order`. Tap opens the app (`Hint::Screen`).
- Badges: an app may expose `int badge()` in its registration (unread count). The launcher
  reads the cached value from `/data/cache/<id>/badge`; it never triggers network I/O itself.
- Swipe down from the status bar opens the quick panel (`Sheet`): Wi-Fi on/off state, battery
  detail, "Sleep now" (power off), "Clean screen" (full Quality refresh).
- On boot, if `config.launcher.resumeLastApp` is true (default) and the device was powered off
  from inside an app, the launcher opens that app directly.
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
