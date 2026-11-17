// Device HAL implementations on M5Unified / Arduino. Only platform/device may include this.
#pragma once

#include <cstdint>
#include <deque>

#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/net.h"
#include "hal/power.h"

namespace pos::device {

class Display : public hal::Display {
 public:
  void push(const Canvas& frame, Rect area, Refresh mode) override;
  // busy() means the update queue of M5GFX is full (back-pressure); a running waveform cannot be
  // read from M5GFX.
  bool busy() override;
  void wait() override;
  // Debug (">set epd-timing on"): wait after each push and log how long it took. Blocks the UI.
  void setTiming(bool on) { timing_ = on; }
  // ">set epd-keep off": the stock driver rule again, every pixel of a pass driven when the mode changes (the flashes this
  // was patched for), to compare on the panel. On by default.
  void setKeepUnchanged(bool on);

 private:
  bool timing_ = false;
};

class Input : public hal::Input {
 public:
  // Call once per loop, after M5.update(): turns the touch state into Down/Move/Up events.
  void update();
  // Queue an event as if the panel had produced it (remote control, P1-8).
  void inject(const hal::TouchEvent& ev);
  bool poll(hal::TouchEvent& out) override;

 private:
  std::deque<hal::TouchEvent> queue_;
};

class Clock : public hal::Clock {
 public:
  uint32_t millis() override;
  int64_t now() override;
  void set(int64_t unixSeconds) override;
};

// WiFi STA + HTTPClient (TLS with the built-in certificate bundle, one kept-alive connection, bodies capped at 512 KB).
class Net : public hal::Net {
 public:
  bool wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs, const std::string& bssid = "",
                   int channel = 0) override;
  void wifiOff() override;
  bool wifiConnected() override;
  int rssi() override;
  hal::WifiLink link() override;
  std::vector<hal::WifiNetwork> scan() override;
  hal::HttpResponse request(const hal::HttpRequest& req) override;
  int64_t syncTime(uint32_t timeoutMs) override;
  void setPowerSave(bool on) override;
};

// Battery (8 samples averaged, 5 % steps), light sleep with touch + timer wake, power off.
class Power : public hal::Power {
 public:
  int batteryMv() override;
  int batteryPercent() override;
  bool charging() override;
  bool usbConnected() override;
  bool canLightSleep() override { return true; }
  hal::Wake lightSleep(uint32_t maxMs) override;
  void powerOff(uint32_t wakeAfterSec) override;  // > 0: RTC alarm power-on after that many seconds
};

struct StorageInfo {
  bool sdOk = false;
  uint32_t sdTotalMb = 0, sdFreeMb = 0;
  bool dataOk = false;
  uint32_t dataTotalKb = 0, dataFreeKb = 0;
};

// Mounts the microSD at /sd (SPI pins in docs/HARDWARE.md, 25 MHz) and LittleFS at /littlefs
// (formatted on first boot), logs the sizes. A missing card is logged ("no SD card"), not fatal.
StorageInfo mountStorage();

// Returns when no chime is playing (hal::chime, sound.cpp), after a second at most: before light sleep and power-off.
void waitForSound();
void chime();  // hal::chime

}  // namespace pos::device
