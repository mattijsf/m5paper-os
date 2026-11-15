// HAL: touch input. Device: M5.Touch (GT911). Simulator: mouse or script.
#pragma once

#include <cstdint>

namespace pos::hal {

struct TouchEvent {
  enum Type : uint8_t { Down, Move, Up };
  Type type = Down;
  int16_t x = 0, y = 0;  // logical screen coordinates (portrait, 540x960)
  uint32_t ms = 0;       // Clock::millis() when the event happened
};

class Input {
 public:
  virtual ~Input() = default;
  // Non-blocking: returns true and fills `out` if an event is waiting.
  virtual bool poll(TouchEvent& out) = 0;
};

}  // namespace pos::hal
