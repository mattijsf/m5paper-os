// HAL: battery and sleep. Device: M5.Power + ESP32 sleep. Simulator: fake values.
#pragma once

#include <cstdint>

namespace pos::hal {

enum class Wake : uint8_t { Touch, Timer, Button, Other };

class Power {
 public:
  virtual ~Power() = default;
  virtual int batteryMv() = 0;
  virtual int batteryPercent() = 0;
  virtual bool charging() = 0;
  virtual Wake lightSleep(uint32_t maxMs) = 0;       // returns on touch, button or timeout
  virtual void powerOff(uint32_t wakeAfterSec) = 0;  // 0 = until the button is pressed
};

}  // namespace pos::hal
