# Architecture

PaperOS is one firmware image for the M5PaperS3 plus a desktop simulator built from the same
source. "OS" here means: a launcher, an app framework, a UI kit, a display manager tuned for
e-ink, and system services (Wi-Fi, storage, time, power). Apps are C++ classes compiled into the
image. There is no dynamic loading.

Decisions and their reasons are in `DECISIONS.md`. Read that before proposing a different stack.

## Layers

```
+---------------------------------------------------------------+
| apps/      launcher  reader  home  inbox  settings  (yours)   |  one folder per app
+---------------------------------------------------------------+
| ui/        widgets, layout, theme, text, icons, keyboard      |  docs/UI.md
+---------------------------------------------------------------+
| core/      App framework + navigation, event loop, timers,    |
|            DisplayManager, services: net, config, storage,    |  docs/APPS.md, DISPLAY.md,
|            time, power policy, log                            |  POWER.md
+---------------------------------------------------------------+
| hal/       interfaces only: Display, Input, Net, Power, Clock |
+-------------------------------+-------------------------------+
| platform/device/              | platform/sim/                 |
| M5Unified, Arduino, ESP-IDF   | SDL2, libcurl, host files     |  docs/SIMULATOR.md
+-------------------------------+-------------------------------+
```

**The one rule that keeps the simulator honest:** nothing under `src/core`, `src/ui`, `src/apps`
or `src/hal` may include `Arduino.h`, `M5Unified.h`, `SDL.h`, ESP-IDF headers or `<curl/...>`.
Those live only under `src/platform/`. The only third-party headers the shared code may use are
`M5GFX.h` (sprite drawing and fonts work on both platforms), `ArduinoJson.h`, and the C++
standard library. `make check` builds both targets, which catches violations.

## Directory layout

```
platformio.ini        two environments: sim (native) and papers3 (device)
partitions.csv        flash layout
Makefile              developer commands (make help)
docs/                 all documentation; ROADMAP.md + STATUS.md drive the work
src/
  core/               gfx.h (types), app framework, display manager, services
  ui/                 widget kit
  apps/<name>/        one folder per app
  apps/common/        what several apps share: the Home Assistant error screen, the daily games' clock and score helpers
  hal/                abstract interfaces (headers only)
  platform/device/    main.cpp + HAL implementations on M5Unified
  platform/sim/       main.cpp + HAL implementations on SDL/host
  platform/posix/     HAL implementations on BSD sockets, built for both (the HTTP server of Files over Wi-Fi, D30)
  gen/                generated fonts and icon tables. Never edit by hand (tools/gen-assets.sh)
assets/               source TTF fonts, icon list
tools/                asset generation, UI test runner, book fetch, USB file push
tests/
  ui/                 simulator scripts (*.script) and golden screenshots
  fixtures/http/      canned HTTP responses for offline simulator runs
  unit/               host unit tests for pure logic (pagination, JSON mapping, zip)
sim-data/
  sd/                 stands in for the microSD card (books/ etc.)
  data/               stands in for the internal LittleFS (config.json, caches)
ha/                   Home Assistant package YAML for the inbox feeds (docs/BACKEND.md)
backup/               factory firmware image (not in git)
out/                  screenshots and test output (not in git)
```

Folders that do not exist yet are created by the roadmap task that needs them.

## Runtime model

### Threads

| Thread | Owner | Does |
|---|---|---|
| UI thread | Arduino `loopTask` on the device, `main()` in the simulator | Everything visible: input dispatch, app code, drawing into the canvas, DisplayManager. Owns all app and widget state. |
| Worker | `core/worker` (one `std::thread`, 16 KB stack on the device) | Blocking jobs: HTTP requests, EPUB indexing. Never touches the canvas or widgets. |
| EPD task | M5GFX (device only) | Plays e-ink waveforms. We only queue regions to it. |

Rule: app code is single-threaded. To do something slow, an app calls
`ctx.async(job, onDone)`. `job` runs on the worker and returns a value; `onDone(value)` runs
later on the UI thread. Capture by value in `job`. If the app was closed meanwhile, `onDone` is
dropped (the framework checks a generation counter).

### Main loop (UI thread)

```
loop:
  1. drain input events (touch down/move/up, button)   -> gesture recogniser -> focused screen
  2. run completed worker callbacks
  3. run due timers
  4. if anything was invalidated: draw dirty widgets into the canvas, DisplayManager.commit()
  5. idle: PowerPolicy.idle()   (device: light sleep until touch, next timer, or worker done;
                                 simulator: SDL_WaitEventTimeout)
```

There is no fixed frame rate. Nothing is drawn unless something was invalidated.

As built (P1-7; with a `Surface` set, touches go through `GestureRecognizer` to it and step 4 calls `Surface::render`): `Os::loopOnce()` runs steps 1-4 (input, `Worker::runCompleted()`,
`Timers::runDue()`, commit + `DisplayManager::tick`); `Loop::iterate()` (`core/loop.h`) adds step 5:
`PowerPolicy::idle(Os::idleWaitMs())`, where the wait is the time to the next timer or
DisplayManager deadline (0 if work is waiting, at most 1 s). Device policy: `delay(min(wait, 10))`
(touch is polled each pass; light sleep comes with P3). Simulator policy: wait for an SDL event, at
most 16 ms; a finished worker job pushes an SDL event (`Os::setWakeup`). `Timers` (`after`, `every`,
`cancel`; earliest first; callbacks may add timers, which wait for the next pass) and `Worker`
(`async(job, onDone)`, FIFO, `cancelAll()` bumps a generation so stale callbacks are dropped).
The worker thread is started by `Os::setup()`; the device sets its stack to 16 KB with
`esp_pthread_set_cfg` first (costs ~20 KB of internal heap: 282 KB -> 245 KB free).

### Rendering path

Widgets draw into one full-screen 8-bit grayscale canvas (`pos::Canvas`, an `LGFX_Sprite`,
540x960 = 518 KB in PSRAM). The DisplayManager compares the canvas with what the panel last
showed, and pushes only changed rectangles, each with a refresh mode. Details: `DISPLAY.md`.

## HAL interfaces

Defined in `src/hal/*.h` (task P1-1). Sketch (final signatures are in the headers):

```cpp
namespace pos::hal {

struct TouchEvent { enum Type : uint8_t { Down, Move, Up } type; int16_t x, y; uint32_t ms; };

class Display {                 // device: M5.Display   sim: SDL window + e-ink emulation
 public:
  virtual void push(const Canvas& frame, Rect area, Refresh mode) = 0;  // queue a region
  virtual bool busy() = 0;                                              // waveform still running
  virtual void wait() = 0;
};

class Input {                   // device: M5.Touch (GT911)   sim: mouse or script
 public:
  virtual bool poll(TouchEvent& out) = 0;       // non-blocking
};

class Clock {                   // device: millis() + BM8563 RTC   sim: real or scripted time
 public:
  virtual uint32_t millis() = 0;
  virtual int64_t now() = 0;                    // unix seconds, 0 if never set
  virtual void set(int64_t unixSeconds) = 0;
};

struct HttpRequest  { std::string method, url, body; std::vector<std::pair<std::string,std::string>> headers; uint32_t timeoutMs = 10000; };
struct HttpResponse { int status = 0; std::string body; std::string error; };   // status 0 = transport error

class Net {                     // device: WiFi + HTTPClient   sim: host network or fixtures
 public:
  virtual bool wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs) = 0;
  virtual void wifiOff() = 0;
  virtual bool wifiConnected() = 0;
  virtual int  rssi() = 0;
  virtual std::vector<WifiNetwork> scan() = 0;
  virtual HttpResponse request(const HttpRequest&) = 0;   // blocking: worker thread only
};

enum class Wake : uint8_t { Touch, Timer, Button, Other };

class Power {                   // device: M5.Power   sim: fake values, settable from scripts
 public:
  virtual int  batteryMv() = 0;
  virtual int  batteryPercent() = 0;
  virtual bool charging() = 0;
  virtual Wake lightSleep(uint32_t maxMs) = 0;  // returns on touch or timeout
  virtual void powerOff(uint32_t wakeAfterSec) = 0;   // 0 = until the button is pressed
};

struct Platform { Display& display; Input& input; Clock& clock; Net& net; Power& power; const char* name; };

void logWrite(const char* line);   // log sink, defined by each platform (stderr / Serial)

}  // namespace pos::hal
```

`Platform` is built in each platform's `main.cpp` and handed to `pos::Os` (`core/os.h`), which
owns the frame canvas and runs `setup()` / `loopOnce()`. `name` is shown in the demo footer.
Until P1-2 and P1-3 the HAL classes in the two `main.cpp` files are thin: Net, Power and Clock
(device wall time) are stubs.

**Files** need no HAL class. Both platforms have POSIX file APIs: on the device the SD card is
mounted at `/sd` and LittleFS at `/littlefs` through the ESP-IDF VFS, so `fopen`, `opendir` and
`std::ifstream` work. Shared code uses virtual paths `/sd/...` and `/data/...` and calls
`pos::fs::resolve(path)` (core/fs.h) which maps them:

| Virtual | Device | Simulator |
|---|---|---|
| `/sd/...` | `/sd/...` (microSD) | `sim-data/sd/...` |
| `/data/...` | `/littlefs/...` (internal flash, 5.9 MB) | `sim-data/data/...` |

`pos::fs` API: `resolve`, `exists`, `readFile`, `writeFile` (atomic: `<path>.tmp` + rename, creates
parent folders), `listDir` (sorted `DirEntry{name,isDir,size}`), `mkdirs`, `setRoots` (tests).
`resolve` returns "" for paths outside `/sd` and `/data` and for any `..` component. The
simulator roots are relative to the working directory: run it from the repository root.
## Storage layout

```
/sd/books/                    EPUB and TXT files the user copies onto the card
/sd/books/.paperos/<id>/      per-book cache: page index, extracted cover, progress
/sd/paperos_config            secrets, KEY=VALUE: HA_TOKEN, HA_URL, WIFI_SSID, WIFI_PASSWORD (read-only for us)
/sd/paperos/home.json         optional override of the Home app layout
/data/config.json             settings (no secrets)
/data/wifi.json               Wi-Fi networks added on the device
/data/home.json               Home app layout (tiles -> entities), see docs/apps/home.md
/data/cache/                  last fetched feeds, so apps can show stale data instantly
/data/log.txt                 ring log, capped
```

Config is JSON read with ArduinoJson. `core/config` exposes typed getters with defaults; a
missing file or key never crashes. Schema: `docs/apps/settings.md`.

## Flash layout (16 MB, `partitions.csv`)

| Partition | Size | Purpose |
|---|---|---|
| nvs | 20 KB | Wi-Fi calibration, Arduino preferences |
| otadata | 8 KB | which app slot boots |
| app0 / app1 | 5 MB each | firmware, two slots for over-the-air update (task P7-3) |
| storage (LittleFS) | 5.9 MB | `/data` |
| coredump | 64 KB | crash dumps |

Firmware size budget: **under 3 MB** of the 5 MB slot. 2026-11-16: 2.31 MB (code 1.6 MB, fonts and icons 0.47 MB, other
data 0.26 MB) after the size pass of DECISIONS.md D31, which also says what keeps it small: no C++ exceptions, no iostreams,
a trimmed ESP-IDF. `make fw` prints the size; if a task pushes it past 3 MB, stop and look at the linker map
(`.pio/build/papers3/firmware.map`) before continuing: one include can cost 150 KB.

## Memory budget

Internal RAM (~300 KB free after boot) is for stacks, Wi-Fi and TLS. Everything large goes to
PSRAM (8 MB), which `malloc` uses automatically for blocks over 4 KB once PSRAM is enabled;
sprites use it via `setPsram(true)`.

| Consumer | Size | Where |
|---|---|---|
| Canvas (8-bit) | 518 KB | PSRAM |
| Shadow of last pushed frame (4-bit packed) | 259 KB | PSRAM |
| M5GFX EPD buffers | ~1 MB | PSRAM/DMA, allocated by the driver |
| Reader: one chapter of text + layout | up to 1 MB | PSRAM |
| JSON documents (HA, inbox) | up to 256 KB | PSRAM |
| Worker stack | 16 KB | internal |
| TLS session | ~45 KB | internal |

Rules: no large stack arrays (UI thread stack is 8 KB); no unbounded `std::string` growth from
network input (cap response bodies at 512 KB); check every allocation that can exceed 64 KB.

## Third-party libraries

| Library | Version | Used for | Where |
|---|---|---|---|
| M5GFX | 0.3.0 | sprite drawing, fonts, PNG/JPEG decode, device EPD + touch drivers | shared |
| M5Unified | 0.3.0 | board bring-up, power, RTC, touch, SD | device only |
| ArduinoJson | 7.4.2 | JSON: `core/store` (app key/value files), config (P2-6) | shared (added in P2-4) |
| miniz (tinfl only) | 3.x, vendored in `lib/miniz` | EPUB unzip | shared (add in task P4-3) |
| SDL2 | Homebrew (`sdl2-compat`) | window and input | sim only |
| libcurl | macOS system | HTTP | sim only |

Pin exact versions in `platformio.ini`. Do not add a library without an entry in `DECISIONS.md`.

## Code conventions

- C++17, namespace `pos` (`pos::ui`, `pos::hal`, `pos::apps::<name>`). Two-space indent,
  120 columns, `clang-format` default Google style with `ColumnLimit: 120`.
- No exceptions, no RTTI-dependent code (they are disabled on the device). Fallible functions
  return `bool` or a small result struct.
- No `delay()` and no blocking I/O on the UI thread.
- Colours are `pos::Color` (`uint32_t` 0xRRGGBB) and only the 16 grays from `gray(0..15)`.
  Passing a `uint8_t` or `uint16_t` colour to LovyanGFX silently selects RGB332 / RGB565.
- Logging: `POS_LOG("tag", "fmt", ...)` (core/log.h): serial on the device, stderr in the sim.
- **One copy of every helper** (2026-11-17, after a review found thirteen UTF-8 walkers and five date routines). Before
  writing a small helper, look here; a second app that needs one moves it here instead of copying it:

  | Need | Where |
  |---|---|
  | UTF-8: decode (strict), append, valid, length, prefix (first n characters), truncate (bytes, at a boundary) | `core/utf8.h` |
  | trim, lower case, starts/ends with, split and join (also of numbers) | `core/strings.h` |
  | day numbers and dates, "14:05", weekday and month names, "is the clock set" | `core/timefmt.h` |
  | CRC-32 | `core/crc32.h` |
  | paths: one spelling (`normalize`), `isDir`, a write or rename that keeps the old file until the new one is there | `core/fs.h` |
  | "owed since" marks in a file | `core/time_mark.h` |
  | an app's launcher badge | `appBadge` / `setAppBadge` in `core/store.h` |
  | sheets (`openSheet`, `confirmSheet`, `Sheet::addButton`), a paged list (`PagedList`), `Widget::removeAll` | `ui/panels.h`, `ui/list.h`, `ui/widget.h` |
  | the screen for "Home Assistant could not be read" | `apps/common/ha_error_state.h` |
  | daily games: today's date, "Klok zetten", copy the score | `apps/common/daily_game.h` |
- Strings shown to the user are English, kept in the app that shows them. Text from Home
  Assistant (entity names in `home.json`) is shown as-is, in Dutch.
