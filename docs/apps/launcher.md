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
