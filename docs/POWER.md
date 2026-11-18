# Power

The battery is 1800 mAh. Running with Wi-Fi on draws about 154 mA (M5Stack docs): under 12
hours. So the design is: **be asleep almost always, keep Wi-Fi off almost always.** The e-ink
image stays visible while the processor sleeps, so the user cannot tell.

Hardware constraints behind this design are in `HARDWARE.md` ("Power behaviour"). The key one:
touch can wake the ESP32 from **light sleep** only, not from deep sleep or power-off.

## States

| State | What is on | Entered when | Left by | Expected current |
|---|---|---|---|---|
| **Active** | CPU, maybe Wi-Fi | input, worker job running, display busy | nothing left to do | 40-160 mA |
| **Idle** (light sleep) | RAM kept, touch controller, RTC | UI thread has nothing to do | touch (GPIO 48), next timer, max 60 s tick | est. 1-5 mA, **measure** (P3-5) |
| **Off** (PMS power-off) | RTC only | auto-off timeout, the launcher's power button, quick panel "Sleep now", battery below 3.3 V | side button; RTC alarm if a wake time was set | ~9 uA (docs) |

There is no separate "deep sleep" state: power-off is lower and wakes on the same sources.

**As built (P3-1)**: `device::Power` (`platform/device/power.cpp`): battery = 8-sample average in 5 %
steps, `lightSleep(maxMs)` = `Display.waitDisplay()` + `M5.Power.lightSleep(us, touch)` returning the wake
cause from `esp_sleep_get_wakeup_cause()` (timer / GPIO = touch / other), `powerOff(sec)` = `timerSleep(sec)`
(RTC alarm power-on) or `M5.Power.powerOff()` for 0, `usbConnected()` = `HWCDC::isPlugged()`. Settings >
Developer has "Sleep test" (10 s, toast with the wake cause) and "Power off" (confirmation sheet). Debug
commands: `>set sleep <ms>`, `>set poweroff <sec>`. **USB and light sleep: see HARDWARE.md** (the serial link does
not survive a sleep and nothing sent during it is received).

### Idle rules (`core/power_policy`, task P3-4)

The main loop calls `PowerPolicy.idle()` when it has nothing to do. It light-sleeps when all of
these hold, otherwise it waits 10 ms and returns:

- no worker job is running and no callback is queued;
- the display is not busy and no settle or clean pass is due within 50 ms;
- Wi-Fi is off (Wi-Fi drops in light sleep unless modem sleep is configured; v1 keeps it simple);
- at least 300 ms have passed since the last touch event (lets double taps and swipes finish).

Sleep duration = time until the next timer (settle pass, status bar minute tick, auto-off),
capped at 60 s. On touch wake, the GT911 still holds the touch data, so the tap that woke the
device is delivered normally: the user never has to "tap twice".

**As built (P3-4)**: `core/power_policy.{h,cpp}`. `PowerPolicy::idle(maxWaitMs)` (called by `Loop`) takes the
platform's short wait as a callback and decides with the pure functions `decideSleep` and `autoOffDue`. It
sleeps when the platform can (device only), no worker job runs or waits, the panel is not busy, nothing is
dirty, no finger is down, Wi-Fi is off, 300 ms have passed since the last touch, and **no USB host is connected
(`power.stayAwakeOnUsb`, default true), nor was one seen in the last 10 minutes (`kUsbHostHoldMs`: the reading drops out now and then,
and a Mac suspends a port nobody has open, so the device napped between two tool runs and the next command waited up to a minute,
2026-11-15). An unplugged device therefore starts light-sleeping ten minutes after the cable is pulled: start battery sessions
(P3-5) after that**. The sleep lasts until the next deadline (timer, settle or clean pass,
long press, auto-off), at most 60 s, and not at all if that is within 50 ms. Reason for the USB rule: light
sleep kills the USB serial session and drops everything sent during it (HARDWARE.md), so a sleeping device
cannot be driven by `tools/device.py`. Setting it to false (Settings > Power, or in `config.json`) lets the
device sleep on USB too. Auto-off: `power.autoOffMin` (default 15), `autoOffMinReader` (30, while the reader app
`read` is open), 0 = never; **never while a USB host is connected**; then `Surface::prepareForPowerOff()` (the
Shell writes `lastApp` to `/data/system.json` and closes the app so it saves), the panel finishes, and
`power.powerOff(0)`. Resuming the last app and the sleep screen come with P3-6.

### Auto-off

- After `power.autoOffMin` minutes without input (default 15, reader 30, 0 = never while on USB):
  draw the sleep screen (below), `display.wait()`, then `power.powerOff(0)` (or with an RTC alarm: "Scheduled refresh" below).
- Not while USB (a host on the data link, or the charger) was seen in the last 12 hours of this boot (`kUsbGraceMs`): a Mac
  suspends a port nobody has open, the device then sees no host, and only the side button could undo an unwanted power-off.
- Before power-off every app gets `onPause()` to save state (reading position).
- Since the idle return (apps/launcher.md, default a minute) an unused app is closed long before auto-off: the device
  switches off from the launcher, except in a book or a game (or with the idle return on Never).
- Boot after power-off is a cold start. Target: **under 2.5 s from button to the previous
  screen** (task P3-6): the launcher restores the last app and the app restores its state from
  `/data`. Do not connect Wi-Fi during boot.

### Sleep screen

A full-screen image drawn before power-off (Quality mode, it stays for hours): large clock-less
layout with the current book cover and progress, or the PaperOS mark, and at the bottom the state in words: the power
icon with "Powered off" (title size, ink) and under it "Press the side button to start". The panel keeps its picture
without power, so this line is the only thing that tells off from on. It is there only when the firmware switched the device off (auto-off, the launcher's power button,
"Sleep now"): **a double click on the side button cuts the power outside the firmware** (HARDWARE.md), and the panel
then keeps whatever was on it. Task P3-6 (simulator part done): `Shell::prepareForPowerOff()` asks the open app for `App::sleepInfo()`
(title, subtitle, percent, cover path; the reader keeps these in its store while a book is open), closes the app, draws
`core/sleep_screen` into the canvas and calls `forceClean()` (one Quality pass); the Shell draws nothing after that.
One caption-sized line above the hint shows the cached unread count (and the time of the last scheduled refresh, see below);
above it, what the phone's agenda has on now or next (`sleepAgendaLine`, D28), as of the moment the screen was drawn.
That is the **plain screen**: what the device shows when there is nothing for the board below, or with the board
switched off.

### Today board (task P7-16, D35)

The device is off most of the day and the panel keeps its picture: with something to show, the sleep screen is a board
of the day instead (`drawSleepBoard`; Settings > Power > "Today on the sleep screen", `power.sleepBoard`, default on).
From the top, each part left out when it has nothing:

- **The date** the board was drawn on ("Thursday 15 January", headline).
- **Weather** of the app's first place, from its saved copy (no read at power-off): today's high and low, the day's
  condition and rain chance ("Rain, 80%", "Cloudy, rain 30%"), and the next four blocks of three hours as on the app's
  second page (hours, icon, temperature, rain chance). Left out when the copy was read more than 24 hours ago or has
  no forecast for today (`weather::fillSleepBoard`).
- **Agenda**: today's events that are not over (an event that is on stays until its end), then the next days with
  events under their name ("Friday 16 January"), six lines in all, never ending on a day's name
  (`sleepAgendaRows`). Times as in the Agenda app's list.
- **The open book** as one row: cover, title, author and percent.
- **Unread per app**: the app's icon and a count, nothing else (BACKEND.md "Privacy"); the book icon counts what there is to read
  later, links that wait and articles nobody opened (apps/reader.md "Articles"); right of it "Updated 15:34" after a scheduled refresh.
- "Powered off" and the hint, as on the plain screen.

Nothing on it reads as "now": no clock, no current temperature, no "Today" or "Tomorrow". Forecasts and clock times
stay true while the picture stands; the date tells how old it is. With no clock, or with neither weather nor agenda,
the plain screen is drawn. Layering: `core/sleep_screen` draws a plain `SleepBoard` (texts and icons) and builds its
agenda part; the weather part comes through `setSleepWeatherSource`, set to the weather app's
`weather::sleepBoardSource` in `apps/apps.cpp` (core does not include apps). Shell and the scheduled refresh both call
`drawSleepScreenFor`. A power-off takes the USB link with it, so the board is read back from a device through the
test screen `sleepboard` (`tools/device.py set screen sleepboard`, then `shot`): the sleep screen as a power-off
would draw it at that moment, without a book. Goldens: `today-board`, `today-board-refreshed`, `today-board-book`, `today-board-off`,
`agenda-sleep` (agenda only), `sched-refreshed` (weather only).

### Resume after power-off

`prepareForPowerOff()` also stores the id of the open app as `lastApp` in `/data/system.json`. The boot path
(`apps::createSurface("", os, true)`) opens that app once and clears the key (a boot after a crash shows the launcher); an
unknown id shows the launcher. The app restores its own state: the reader keeps the open book's file name in its store
(`open`, cleared when the user leaves the book for the library) and reopens it on the saved page; Settings just opens.
No Wi-Fi at boot. `>set screen launcher` never resumes.

## Wi-Fi policy (`core/net`, task P3-2)

- Off at boot. Apps never switch Wi-Fi on themselves: they call `ctx.http(...)` and the net
  service connects if needed. **Several networks**: known = `WIFI_SSID[_2.._8]` from the card + networks added on the
  device (the card wins duplicates). It tries the last network that worked with its stored BSSID and channel (fast path,
  ~1.2 s measured), and if that fails or there is none, scans once and joins the known network with the strongest signal
  (~5.3 s measured). One attempt each, never a loop; a failure is reported to the caller. **A request gets one more
  try** when it failed within 3 s before anything of it left the device (no address for the name, no connection:
  `hal::HttpResponse::notSent`), 0.4 s later when the same call brought Wi-Fi up: the first lookup or connection on a
  link that is seconds old can fail, and the user saw "Home Assistant not reachable" for it (2026-11-18). Safe for
  any method (nothing was sent); a failure that took longer, or a second one, is reported as it is (`NetService::perform`). **2.4 GHz only**: the SSID must
  also exist on 2.4 GHz (mesh routers normally do).
- After the last request finishes, Wi-Fi stays up for `net.lingerSec` (default 20 s) in case
  another request follows, then switches off.
- Teletekst keeps Wi-Fi up for about a minute after a page is shown (`ctx.keepWifi()`, apps/teletekst.md "Saved copies
  and reads"): the next tap is mostly a read, and a reconnect costs 2 s and a TLS handshake. It ends with the screen
  (the idle return leaves the app after a minute without a touch).
- While the Home app is in the foreground and was touched in the last 2 minutes, linger is
  extended so toggles respond fast. The device does not light-sleep during that window.
- Time sync: SNTP after a connect when the clock was never set or the last sync is over a day old, written to the BM8563 RTC on
  the UI thread; the status bar updates at once (measured: the clock was set after the first connect). Nothing connects just
  because the clock is lost (Wi-Fi stays off at boot); apps that need the date offer "Klok zetten" (`ctx.syncClock`, Somdoku,
  the word game), and any other connect (Home, an inbox refresh, Settings > Wi-Fi) sets it too.
- No background polling while the device is on. While it is off, the scheduled refresh below can fetch the inbox feeds (off by default).
- TLS handshakes are expensive (~1.5 s, ~45 KB RAM); free heap measured: ~200 KB idle, ~150 KB with Wi-Fi up, ~104 KB during
  a TLS request. HTTPS to the public Home Assistant address is the expected case (BACKEND.md); the device keeps one
  connection alive per refresh.

### Where the first HTTPS request went (2026-11-15, measured on the device, `[net]` log lines)

"Test connection" (cold: Wi-Fi off, then `GET <HA_URL>/api/` over TLS) read **9.5 s**; the Wi-Fi join was 1.3 s. Phase by phase:

| Phase | Before | After |
|---|---|---|
| Wi-Fi join, fast path (stored BSSID/channel, includes DHCP) | 1.3 s | 1.3 s |
| DNS | 30-45 ms | 30-45 ms |
| TCP + TLS handshake | 0.56-0.76 s | 0.56-0.68 s (CPU was already 240 MHz) |
| request + response headers (server) | 0.5-2.9 s (varies) | 0.5-0.7 s |
| body | 0 | 0 |
| **UI thread stalled, result delivered late** | **~7.5 s** | **0** |
| **Total, cold** | **9.5-10 s** | **2.0-2.6 s** |
| Second request, kept-alive connection | 2.0 s (same stall) | **34 ms** |

The 7.5 s was not the network: **the UI thread blocked in `Serial.print`.** The USB CDC log writes block (default TX timeout) while no
host is reading the port, and the log lines of a request pile up; every `POS_LOG` could stall the loop for ~100 ms. Fix:
`Serial.setTxTimeoutMs(0)` (drop when the buffer is full, never block). This also removes the 2.2 s UI "gap" seen right
before taps and benefits every screen. Smaller changes made on the way: the SNTP wait (up to 5 s) no longer sits in the
connect path (it is its own worker job after the request); the UI thread no longer calls the Wi-Fi HAL (connected state and link
info are cached by the net service: the stack's locks are held during connects and TLS); the device log shows the phases
(`https phases: dns= tls+tcp= request+headers= body= cpu= reused=`) and `[perf] UI loop gap` for passes later than 400 ms.
The handshake itself is ~0.6 s, well under the 2 s worry; the request/response of the Home Assistant server is the rest.

## Scheduled refresh (tasks P7-5 and P7-16, `apps/inbox/scheduled_refresh`)

Settings > Power > "While off, wake to update the sleep screen every" Off / 1 h / 2 h / 4 h / 8 h (`power.refreshHours`, default 0 = off).

- Every power-off for sleep (auto-off, "Sleep now") goes through `Os::powerOffForSleep()`: with the setting on **and the clock set** it
  powers off with an RTC alarm (`power.powerOff(hours * 3600)`) and remembers the alarm time as `wakeAt` in `/data/system.json`.
  Without a clock there is no alarm: the wake could not be told from the side button.
- Boot (`apps::createBootSurface`): if the clock is within -90 s .. +180 s of `wakeAt` the boot is a scheduled wake
  (`isScheduledWake`; the RTC flag is not read, so a side-button press inside that window also counts: a touch during the refresh
  ends it in a normal boot). `wakeAt` is forgotten at every boot, so an earlier side-button press is an ordinary boot.
- A scheduled wake builds no launcher and no app. It keeps the panel as it is (no first Quality refresh), reads the relay
  (`GET /api/states/<entity>`, one Wi-Fi attempt as always: the inbox feeds and the agenda), writes each feed's cache and badge file
  (`/data/cache/<id>/badge` = items newer than that app's seen marker), then, with the Today board on, reads the weather's
  first place as the app does (`weather::refreshSaved`: the entity, two forecasts, the sun, the minute forecast, on the
  connection that is open; not asked when the relay's request found no network), then redraws the sleep screen of the last
  power-off (the book kept as `sleep.*` in `system.json`; date, agenda and weather as of this hour) with "Updated 14:05",
  in one Quality pass, and powers off with the next alarm. Before the redraw, links that wait to be read later are read
  and saved (apps/reader.md "Articles", P7-18: `reader::fetchArticles` with 25 s; no read starts with under 5 s of that
  left, and none waits longer than what is left), so an article shared in the evening is on the card in the morning and
  the board's count is right. If neither the relay nor the weather answered the panel is left alone. Hard limit: 45 s
  awake, then off.
- The sleep screen shows a count only, never a sender or text. The count is also drawn at an ordinary power-off when badges are
  cached.
- Log: one line per wake in `/data/refresh.log`, `unix,msSinceBoot,refreshMs,feedsAnswered,unread,weatherRead,articlesSaved`
  (`tools/device.py cat /data/refresh.log`; lines from before P7-16 have five fields, before P7-18 six).
- Test without stranding the device: `tools/device.py poweroff N` (RTC wake after N s, the refresh runs, then an ordinary boot) and
  `poweroff N M` (the refresh powers off once more for M s, the wake after that stays on).

**Measured on the device (2026-11-15, four RTC wakes on USB power, known network on the fast path, HTTPS to the public address)**:
refresh 2.8 / 5.2 / 2.9 / 3.0 s from the first loop pass to "done" (Wi-Fi join 1.2-1.4 s, first HTTPS request 1.2-3.4 s of which
TLS 0.6-2.9 s, second request 37 ms on the kept-alive connection); 3.5 / 6.1 / 3.6 / 3.7 s since `setup()`. Not in those numbers:
ROM and bootloader before `setup()` (about 1.3 s, from the time between two chained wakes) and the Quality refresh that plays
before the power goes (1.5-1.9 s). Total per wake: **about 6.5 s, up to 9 s** with a slow TLS handshake.

**With the weather (P7-16, 2026-11-18, one RTC wake on USB power)**: refresh 4.2 s (5.0 s since `setup()`): the relay's
request 1.0 s with its TLS handshake, then the weather's five requests 34-60 ms each on the same connection, about
0.25 s together. Inside the range above; the estimate below stands.

**Estimate (not measured: there is no current meter)**: at an assumed 120 mA average while awake with Wi-Fi up, one wake costs
about 0.2 mAh (0.15-0.3). Per day: 1 h = ~5 mAh, 2 h = ~2.5 mAh, 4 h = ~1.3 mAh, 8 h = ~0.6 mAh, against 1800 mAh: well under
1% a day. Bad case: Home Assistant does not answer and every wake runs to the 45 s limit, about 1.5 mAh per wake (1 h: ~36 mAh a
day, 2%).

## CPU

`setCpuFrequencyMhz(80)` when Wi-Fi is off (rendering text at 80 MHz is fine; measure page turn
time before and after, task P3-5). 240 MHz while Wi-Fi is on or while indexing a book.

## Battery reading

No fuel gauge: `M5.Power.getBatteryVoltage()` (ADC on GPIO 3) and `getBatteryLevel()`.
Voltage under load sags, so sample only when Wi-Fi is off, average 8 samples, and show steps of
5%. `M5.Power.isCharging()` reads GPIO 4. Measured at first boot: 3716 mV = 52% while charging.

## Estimates (to be replaced by measurements in task P3-5)

| Scenario | Assumption | Average | Battery life |
|---|---|---|---|
| Reading | 1 page / 40 s: 1 s active at 60 mA, rest idle at 3 mA | ~4.5 mA | ~400 h of reading |
| Home app, active use | Wi-Fi up, 100 mA | 100 mA | 18 h continuous |
| Checking inbox | 15 s Wi-Fi per check, 10 checks a day | ~0.5 mAh per check | negligible |
| Off | 9 uA | | years (self-discharge dominates) |
| Off with scheduled refresh | ~6.5 s awake at 120 mA per wake (time measured, current assumed) | ~0.2 mAh per wake | 2 h interval: ~2.5 mAh a day |

## Measuring (task P3-5)

1. Developer screen shows battery mV and time since boot; `/data/power.csv` gets a line every
   10 minutes of awake/idle time: `unix,mv,state,wifiSeconds,pushes`.
   **Built (2026-11-15, `core/power_log`)**: the line is `unix,mv,state,wifiSeconds,pushes,usb,app` (state `active` = touched in the last
   minute, else `idle`; `wifiSeconds` and `pushes` count since boot; `usb` 1 when a host or charger is seen; `app` = id of the open app, empty
   on the launcher). One line at boot, then every 10 minutes: the loop wakes from light sleep for it. On by default (`power.log` in
   config.json; switch in Settings > Developer, which also shows battery mV / percent / USB, time since boot, lines written and Wi-Fi
   seconds). At 200 KB the file becomes `/data/power.old.csv`. Read it over USB: `tools/device.py cat /data/power.csv` (`>cat` reads only
   these two files). A battery run starts by unplugging the device; nothing else to set. Not built: the 40 s auto page turn for the
   reading session.
2. Run three sessions from a full charge with USB unplugged: 2 h reading (auto page turn every
   40 s from a script flag), 2 h idle on the launcher, 30 min Home app with Wi-Fi up. Compute
   mAh/h from the voltage curve and compare with the table. With a USB power meter in line the
   numbers are better: use one if available.
3. Record results in `STATUS.md` and correct this file.
