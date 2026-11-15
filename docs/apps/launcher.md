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
`/data/cache/<id>/badge` in `onShow()`. Not built yet: the summary line, the quick panel
(swipe down), resume-last-app. Placeholder apps (`src/apps/placeholders.cpp`: Read, Home, WhatsApp,
Mail, Settings) show an `EmptyState` "Coming soon" until the real apps replace them. UI test:
`tests/ui/launcher.script` (open an app, back, long press on the back arrow).

## Behaviour

- Tiles come from the app registry sorted by `order`. Tap opens the app (`Hint::Screen`).
- Badges: an app may expose `int badge()` in its registration (unread count). The launcher
  reads the cached value from `/data/cache/<id>/badge`; it never triggers network I/O itself.
- Swipe down from the status bar opens the quick panel (`Sheet`): Wi-Fi on/off state, battery
  detail, "Sleep now" (power off), "Clean screen" (full Quality refresh).
- On boot, if `config.launcher.resumeLastApp` is true (default) and the device was powered off
  from inside an app, the launcher opens that app directly.
- The status bar clock updates once per minute with `Hint::Content` on its own rect only.

## Files

`src/apps/launcher/launcher.cpp`, `src/ui/status_bar.cpp`, `src/ui/quick_panel.cpp`.
UI test: `tests/ui/launcher.script`.
