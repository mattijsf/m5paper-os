// HAL: time. Device: millis() + BM8563 RTC. Simulator: host clock, or scripted time.
#pragma once

#include <cstdint>

namespace pos::hal {

class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t millis() = 0;                 // monotonic milliseconds since boot
  virtual int64_t now() = 0;                     // unix seconds, 0 if never set
  virtual void set(int64_t unixSeconds) = 0;
};

}  // namespace pos::hal
