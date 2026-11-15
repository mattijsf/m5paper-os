// Device HAL implementations on M5Unified / Arduino. Only platform/device may include this.
#pragma once

#include <cstdint>
#include <deque>

#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
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

}  // namespace pos::device
