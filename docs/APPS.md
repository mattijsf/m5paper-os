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

**Staying open** (apps/launcher.md "Idle return"): an app nobody touches for a minute (`launcher.homeAfterSec`) is left
for the launcher. An app one looks at for minutes without a touch, a book or a game, registers with one more field,
`.stayOpen = true` (after `.order`). A single screen that is in use without a finger (a code being compared with the
phone, files arriving) overrides `bool staysOpen() const` instead; it is asked at every look, so it may change. Text
being typed is safe by itself: nothing leaves while the on-screen keyboard shows.

## As built (P2-4)

`core/app.h` (App, Screen, Nav, AppContext, registry, `POS_REGISTER_APP`), `core/shell.{h,cpp}` (the
Shell: status bar, title bar, a stack of screens with the launcher at the bottom; it is the `Surface`
the Os shows), `core/store.{h,cpp}` (`Store`, `Cache`), `ui/chrome` (StatusBar, TitleBar, TabBar),
`ui/widgets.h` (umbrella). The `hello` app of this document is compiled verbatim in
`tests/unit/app_test.cpp` and tested. Details that go beyond the text above:

- `pos::Screen` is the app-level screen; the widget surface of the whole panel is `ui::Root`.
  `Screen::build(root)` gets a `ui::Column` with 24 px side margins inside the content area; the
  title bar (back arrow, title; long press on the arrow = launcher) is shown for every app screen
  and hidden for the launcher. A screen that returns `true` from `fullScreen()` gets the whole panel instead: no
  status bar, no title bar, no side margins (the reader's page); it then draws its own way out (the reader's menu).
  `Screen::sideMargin()` changes the side margins of a screen that keeps the bars (Teletekst: 10 px, its grid of 40
  characters is 520 px wide); everything else stays at 24.
  `Screen::debugState()` is a one-line description for UI scripts (`expect-state`, `page-through`).
- `nav.push/pop/home` run after the current gesture or callback returns, so a button can pop its own
  screen. Back from an app's first screen leaves the app. Order when leaving: top screen `onHide`,
  `App::onStop`, then the screens and the App are destroyed (and the store is saved).
- `ctx.config` (read-only `Config`) and `ctx.secrets` (`Secrets`: `present`, `get`; never log or show values) exist since P2-6.
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

New apps: `tools/new-app.sh <id> "<Name>" <Icon>` copies `src/apps/_template/` (excluded from every
build), writes `tests/ui/<id>.script` and a `docs/apps/<id>.md` stub; `make ui-test` then reports the
new screenshot, `make ui-accept` after looking at it. Checked with `tools/new-app.sh demo "Demo" Info`
(a sixth tile appeared; the demo files were deleted afterwards).

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
keep (`ctx.store`) in `onStop()`/`onHide()`. Power-off calls `onStop()` first. So does the idle return: an app left
alone for a minute is closed the same way (above, "Staying open").

## What an app gets: `AppContext`

| Member | Purpose |
|---|---|
| `ctx.nav` | `push<S>(args...)`, `pop()`, `home()` |
| `ctx.async(job, onDone)` | run `job` on the worker thread, deliver its result to `onDone` on the UI thread; dropped if the screen is gone |
| `ctx.background(job)` | run `job` on the worker thread, nothing comes back; it runs even if the screen or the app closes first (save a file, switch something off) |
| `ctx.http(request, onDone)` | convenience over `async`: brings Wi-Fi up, performs the request, applies the Wi-Fi linger policy |
| `ctx.transport()` | for a job that makes several requests in a row (`ctx.async`): what the job calls to send one, with the same Wi-Fi handling as `ctx.http`. One job, one update of the screen (Hacker News: a list is two requests) |
| `ctx.store` | per-app key/value JSON persisted at `/data/apps/<id>.json`: `get<T>(key, default)`, `set(key, value)`, `save()` |
| `ctx.config` | read-only system settings (`config.json`) |
| `ctx.secrets` | read-only secrets by key (`HA_TOKEN`), from `/sd/paperos_config` |
| `ctx.cache` | `read(name)`, `write(name, text)` under `/data/cache/<id>/`: show stale data first |
| `ctx.timer(ms, fn)` | one-shot timer on the UI thread; cancelled with the screen |
| `ctx.syncClock(done)` | the clock was lost (battery ran flat): joins Wi-Fi once and waits up to 12 s for the time sync; `done(ok, error)` |
| `ctx.toast(text)` | 3 s message |
| `ctx.clock` | `now()`, `millis()` |
| `ctx.log` | `POS_LOG` with the app id as tag |

### Opening the WhatsApp compose screen from another app

`#include "apps/inbox/compose.h"`, then `whatsapp::sendToFavorite(ctx, "Family", text)` (or `openCompose` / `openNewMessage`): the compose
screen is pushed onto your app's stack, the user confirms before anything is sent, back returns to your screen (apps/inbox.md "Sending").
To hand text to any app instead, use the clipboard: `clipboard::set(text, "<your app id>")` (`core/clipboard.h`, D21).

## Threads: what an app has to know

Two threads, and an app only ever writes code for the first:

- **The UI thread** runs everything of a screen: `build`, `onShow`, widget handlers, timers, and the `onDone` of
  `ctx.async`. Widgets and the screen's members are touched here and nowhere else, so an app needs no locks.
- **The worker** runs what takes time, one job after the other: `ctx.async(job, onDone)` when the screen wants the
  result, `ctx.background(job)` when nobody waits for it. A job gets copies (capture by value) and touches no widget,
  no `this`, no `ctx`.

What goes where:

| Work | Where | Why |
|---|---|---|
| saved state in `/data`: `ctx.store`, `ctx.cache`, small files (up to 16 KB) | UI thread, in `build()` | `core/fs` answers them from memory after the first read (a file on the flash costs ~9 ms to open); the first frame already shows the data |
| the network (`ctx.http`, `ctx.ha()`), the SD card, big files, anything over a few ms of computing | `ctx.async` | it blocks for hundreds of ms to seconds |
| writing a file after a refresh, switching the radio back, cleaning up | `ctx.background` | a write to the flash is ~35 ms; and it must happen even when the user leaves at that moment |
| the phone pairing with its keys (`companion::loadPairing`), making or opening envelopes | `ctx.async` | deriving the session keys is ~50 ms; a screen that only asks "is a phone paired?" uses `companion::pairedPhone()` |

**The UI thread's stack is 8 KB on the device** (`CONFIG_ARDUINO_LOOP_STACK_SIZE`; the worker's is 16 KB) and the
simulator's is not: a local of a few KB in screen code (a parsed page, a big array) works in every test and overflows on
the device. Keep such things on the heap (`std::make_shared`, a `std::vector`, a member of the screen); Teletekst's 8 KB
`Page` is the example (2026-11-18, found by reading, not by a crash).

**On this panel every extra pass shows** (DISPLAY.md), so the order matters more than on an LCD:

1. **Read what is saved before the first frame, not after it.** "Draw empty, load in the background, draw again" is two
   passes over the screen; reading the saved copy in `build()` is one. This is why small `/data` files are kept in
   memory instead of being made asynchronous.
2. **One result, one change.** In `onDone`, set the new values on the widgets that exist; their setters compare and a
   widget whose text did not change is not drawn again (`Label::setText`, `ListItem::set*`, `PagedList::refill`). Do not
   rebuild the screen for a result that changed two values.
3. **`relayout()` only when sizes change** (rows came or went, a bar appeared). It is a whole-screen pass.
4. **Several results at once, not one after the other**: one job that fetches everything a screen needs (`HaClient::
   getMany`, one relay read) gives one update; three jobs give three.
5. **What changes together is one region.** Three widgets that change in one callback are three regions, and each is a
   push that waits for the one before (280 ms on the panel against 70): invalidate their common parent once
   (`parent->invalidate(Hint::Content)`; Hacker News `oneChange`). And nothing is drawn on the finger going down that
   the tap replaces: a pressed look in flight delays the result (`Pressable`, `PagerBar` and `Segmented` wait
   `kPressDelayMs` for that).
6. A result for a screen that is gone is dropped by itself (`ctx.async`); a job nobody waits for any more is not even
   started when the app closes. What must happen all the same goes to `ctx.background`.

Measured on the device (2026-11-17): leaving an app for the launcher was 134-354 ms before the first pixel because the
launcher read 16 small files on the UI thread; with those in memory it is 15 ms, tap to panel 137 ms instead of 255-476.
The shell logs `[perf] close <app> took N ms: ...` when leaving an app costs 30 ms or more. Opening WhatsApp or Mail
was 53-54 ms before the first pixel: the screen derived the pairing's keys twice only to know whether a phone is
paired; with `pairedPhone()` 18-19 ms (tap to panel 179-197 ms instead of 221-238). **Measure before moving work**:
the first guess there (cleaning the saved messages) cost nothing measurable on the device.

## Rules for app code

1. Never block: no `delay`, no HTTP, no SD card, no file of more than a few KB on the UI thread: `ctx.async` /
   `ctx.http` / `ctx.background` ("Threads" above says what goes where).
2. Never draw directly to the canvas outside a widget's `draw()`; never call the display.
   Change widget state and `invalidate()` with the right hint (`DISPLAY.md`).
3. Use theme tokens and existing widgets (`UI.md`). A new widget goes in `src/ui/` if a second
   app could use it, with a line in the widget table of `UI.md`.
4. Lists paginate with `ui::PagedList` (or `ui::Pager` when a page is more than rows). No scrolling, no animation.
5. Show cached data immediately, then refresh: `ctx.cache.read` in `onShow`, then `ctx.http`.
6. Handle failure visibly: `EmptyState` with a Retry button on first load, `Toast` otherwise. For Home Assistant the
   screen is `apps::addHaErrorState(holder, error, retry)` (`apps/common/ha_error_state.h`): the same words in every app,
   and a missing or refused token is told apart from "cannot reach".
7. Shared code only: no `Arduino.h`, `M5Unified.h`, `SDL.h` (see `ARCHITECTURE.md`).
8. No private copies of helpers: text, dates, paths, sheets, badges are in the table of `ARCHITECTURE.md` "Code
   conventions". The launcher badge is `setAppBadge(ctx.appId(), n)`.

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
| Somdoku (the user's daily sum puzzle) | `apps/somdoku.md` | P7 |
| Word game (daily five-letter word game in Dutch) | `apps/wordgame.md` | P7 |
| Weather (Google Weather through Home Assistant) | `apps/weather.md` | P7 |
| Agenda (the phone's calendar, one way) | `apps/agenda.md` | P7 |
| Teletekst (NOS Teletekst, page by page) | `apps/teletekst.md` | P7 |
| Hacker News (story lists, comments, articles as text) | `apps/hackernews.md` | P7 |
