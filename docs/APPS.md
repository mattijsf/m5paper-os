# Writing apps

An app is a folder under `src/apps/` with one class. Adding an app never requires touching the
core: create the folder, register the class, rebuild. The framework is built in tasks P2-4 and
P2-5; the code below is the contract those tasks implement.

## Minimal app

```cpp
// src/apps/hello/hello_app.cpp
#include "core/app.h"
#include "ui/widgets.h"

namespace pos::apps::hello {

class HelloScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Hello");
    label_ = root.add<ui::Label>("Tap the button", ui::Type::Body);
    root.add<ui::Button>("Count", [this] {
      ++count_;
      label_->setText("Tapped " + std::to_string(count_) + " times");   // invalidates itself
    });
  }

 private:
  ui::Label* label_ = nullptr;
  int count_ = 0;
};

class HelloApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<HelloScreen>(); }
};

POS_REGISTER_APP(HelloApp, {
    .id = "hello",          // unique, lowercase; also the config and cache namespace
    .name = "Hello",        // label under the launcher tile
    .icon = Icon::Info,     // from src/gen/icons.h
    .order = 90,            // position in the launcher (lower first)
});

}  // namespace pos::apps::hello
```

That is the whole app. `POS_REGISTER_APP` adds it to a static registry that the launcher reads.
(The registry uses a static initialiser; `platformio.ini` keeps app object files from being
dropped by the linker. If a new app does not show up in the launcher, check that first.)

## As built (P2-4)

`core/app.h` (App, Screen, Nav, AppContext, registry, `POS_REGISTER_APP`), `core/shell.{h,cpp}` (the
Shell: status bar, title bar, a stack of screens with the launcher at the bottom; it is the `Surface`
the Os shows), `core/store.{h,cpp}` (`Store`, `Cache`), `ui/chrome` (StatusBar, TitleBar, TabBar),
`ui/widgets.h` (umbrella). The `hello` app of this document is compiled verbatim in
`tests/unit/app_test.cpp` and tested. Details that go beyond the text above:

- `pos::Screen` is the app-level screen; the widget surface of the whole panel is `ui::Root`.
  `Screen::build(root)` gets a `ui::Column` with 24 px side margins inside the content area; the
  title bar (back arrow, title; long press on the arrow = launcher) is shown for every app screen
  and hidden for the launcher.
- `nav.push/pop/home` run after the current gesture or callback returns, so a button can pop its own
  screen. Back from an app's first screen leaves the app. Order when leaving: top screen `onHide`,
  `App::onStop`, then the screens and the App are destroyed (and the store is saved).
- `ctx.async` / `ctx.http` results are dropped if the screen that was on top at the call is gone, and
  all pending results are dropped when the app is left. `ctx.http` calls `hal::Net::request` on the
  worker; bringing Wi-Fi up and the linger policy are not implemented yet (P3). `ctx.timer` timers are
  cancelled with the screen. `ctx.config` and `ctx.secrets` arrive with the config service (P2-6, P3-3).
- Registration is a static initialiser in the app's `.cpp`. PlatformIO links `src/` objects directly
  (no archive), so nothing is dropped; no extra linker flag was needed.
- `apps::createSurface(name, os)` (src/apps/apps.cpp) is what the platform mains call: "" = launcher,
  a registered id = that app opened, a UI test screen name, else the old demo.
- The status bar clock shows UTC for now (the time zone setting comes with P2-6); "--:--" while the
  RTC is unset.

## Lifecycle

```
launcher tile tapped
   -> App constructed -> onStart(ctx)        push the first screen
   -> Screen::build(root)                    create widgets (once per screen instance)
   -> Screen::onShow()                       every time the screen becomes visible: start loading
   ...  events, async results, timers  ...
   -> Screen::onHide()                       covered by another screen, or app leaving
   -> App::onStop()                          back from the first screen, or home: save state
   -> App destroyed
```

Only one app is alive at a time. Leaving an app destroys it, so it must save what it wants to
keep (`ctx.store`) in `onStop()`/`onHide()`. Power-off calls `onStop()` first.

## What an app gets: `AppContext`

| Member | Purpose |
|---|---|
| `ctx.nav` | `push<S>(args...)`, `pop()`, `home()` |
| `ctx.async(job, onDone)` | run `job` on the worker thread, deliver its result to `onDone` on the UI thread; dropped if the screen is gone |
| `ctx.http(request, onDone)` | convenience over `async`: brings Wi-Fi up, performs the request, applies the Wi-Fi linger policy |
| `ctx.store` | per-app key/value JSON persisted at `/data/apps/<id>.json`: `get<T>(key, default)`, `set(key, value)`, `save()` |
| `ctx.config` | read-only system settings (`config.json`) |
| `ctx.secrets` | read-only secrets by key (`HA_TOKEN`), from `/sd/paperos_config` |
| `ctx.cache` | `read(name)`, `write(name, text)` under `/data/cache/<id>/`: show stale data first |
| `ctx.timer(ms, fn)` | one-shot timer on the UI thread; cancelled with the screen |
| `ctx.toast(text)` | 3 s message |
| `ctx.clock` | `now()`, `millis()` |
| `ctx.log` | `POS_LOG` with the app id as tag |

## Rules for app code

1. Never block: no `delay`, no file parsing of more than a few KB, no HTTP on the UI thread.
   Use `ctx.async` / `ctx.http`.
2. Never draw directly to the canvas outside a widget's `draw()`; never call the display.
   Change widget state and `invalidate()` with the right hint (`DISPLAY.md`).
3. Use theme tokens and existing widgets (`UI.md`). A new widget goes in `src/ui/` if a second
   app could use it, with a line in the widget table of `UI.md`.
4. Lists paginate with `ui::Pager`. No scrolling, no animation.
5. Show cached data immediately, then refresh: `ctx.cache.read` in `onShow`, then `ctx.http`.
6. Handle failure visibly: `EmptyState` with a Retry button on first load, `Toast` otherwise.
7. Shared code only: no `Arduino.h`, `M5Unified.h`, `SDL.h` (see `ARCHITECTURE.md`).

## Checklist for a new app

1. `tools/new-app.sh <id> "<Name>" <Icon>` (task P2-5) copies `src/apps/_template/` and fills in
   the names. Or copy the folder by hand.
2. If the icon is not in `assets/icons.txt`, add its Lucide name and run `tools/gen-assets.sh`.
3. Write screens. Run `make run` or `program --app <id>`.
4. Add `tests/ui/<id>.script` with at least one `shot` per screen and, if it uses HTTP, fixtures
   under `tests/fixtures/http/`. Run `make ui-test`, look at the images, `make ui-accept`.
5. Write `docs/apps/<id>.md`: purpose, screens, data sources, config keys.
6. `make check`.

## Built-in apps

| App | Doc | Phase |
|---|---|---|
| Launcher (home screen, not in the registry) | `apps/launcher.md` | P2 |
| Settings | `apps/settings.md` | P2, P3 |
| Read | `apps/reader.md` | P4 |
| Home (Home Assistant) | `apps/home.md` | P5 |
| WhatsApp, Mail (two tiles, one implementation) | `apps/inbox.md` | P6 |
