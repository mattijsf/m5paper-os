// Device HAL implementations on M5Unified / Arduino. Only platform/device may include this.
#pragma once

#include <cstdint>
#include <deque>

#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"

namespace pos::device {

class Display : public hal::Display {
 public:
  void push(const Canvas& frame, Rect area, Refresh mode) override;
  bool busy() override;
  void wait() override;
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
