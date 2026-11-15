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
| **Off** (PMS power-off) | RTC only | auto-off timeout, quick panel "Power off", battery below 3.3 V | side button; RTC alarm if a wake time was set | ~9 uA (docs) |

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
(`power.stayAwakeOnUsb`, default true)**. The sleep lasts until the next deadline (timer, settle or clean pass,
long press, auto-off), at most 60 s, and not at all if that is within 50 ms. Reason for the USB rule: light
sleep kills the USB serial session and drops everything sent during it (HARDWARE.md), so a sleeping device
cannot be driven by `tools/device.py`. Setting it to false (Settings > Power, or in `config.json`) lets the
device sleep on USB too. Auto-off: `power.autoOffMin` (default 15), `autoOffMinReader` (30, while the reader app
`read` is open), 0 = never; **never while a USB host is connected**; then `Surface::prepareForPowerOff()` (the
Shell writes `lastApp` to `/data/system.json` and closes the app so it saves), the panel finishes, and
`power.powerOff(0)`. Resuming the last app and the sleep screen come with P3-6.

### Auto-off

- After `power.autoOffMin` minutes without input (default 15, reader 30, 0 = never while on USB):
  draw the sleep screen (below), `display.wait()`, then `power.powerOff(0)`.
- Before power-off every app gets `onPause()` to save state (reading position).
- Boot after power-off is a cold start. Target: **under 2.5 s from button to the previous
  screen** (task P3-6): the launcher restores the last app and the app restores its state from
  `/data`. Do not connect Wi-Fi during boot.

### Sleep screen

A full-screen image drawn before power-off (Quality mode, it stays for hours): large clock-less
layout with the current book cover and progress, or the PaperOS mark, plus "Press the side
button to wake". Task P3-6.

## Wi-Fi policy (`core/net`, task P3-2)

- Off at boot. Apps never switch Wi-Fi on themselves: they call `ctx.http(...)` and the net
  service connects if needed. **Several networks**: known = `WIFI_SSID[_2.._8]` from the card + networks added on the
  device (the card wins duplicates). It tries the last network that worked with its stored BSSID and channel (fast path,
  ~1.2 s measured), and if that fails or there is none, scans once and joins the known network with the strongest signal
  (~5.3 s measured). One attempt each, never a loop; a failure is reported to the caller. **2.4 GHz only**: the SSID must
  also exist on 2.4 GHz (mesh routers normally do).
- After the last request finishes, Wi-Fi stays up for `net.lingerSec` (default 20 s) in case
  another request follows, then switches off.
- While the Home app is in the foreground and was touched in the last 2 minutes, linger is
  extended so toggles respond fast. The device does not light-sleep during that window.
- Time sync: SNTP after a connect when the clock was never set or the last sync is over a day old, written to the BM8563 RTC on
  the UI thread; the status bar updates at once (measured: the clock was set after the first connect).
- No background polling in v1. Scheduled wake-ups to pre-fetch the inbox are task P7-5.
- TLS handshakes are expensive (~1.5 s, ~45 KB RAM); free heap measured: ~200 KB idle, ~150 KB with Wi-Fi up, ~104 KB during
  a TLS request. HTTPS to the public Home Assistant address is the expected case (BACKEND.md); the device keeps one
  connection alive per refresh.

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

## Measuring (task P3-5)

1. Developer screen shows battery mV and time since boot; `/data/power.csv` gets a line every
   10 minutes of awake/idle time: `unix,mv,state,wifiSeconds,pushes`.
2. Run three sessions from a full charge with USB unplugged: 2 h reading (auto page turn every
   40 s from a script flag), 2 h idle on the launcher, 30 min Home app with Wi-Fi up. Compute
   mAh/h from the voltage curve and compare with the table. With a USB power meter in line the
   numbers are better: use one if available.
3. Record results in `STATUS.md` and correct this file.
