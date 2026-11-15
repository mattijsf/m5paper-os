// One-shot and periodic timers, run on the UI thread from the main loop (docs/ARCHITECTURE.md).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/display_manager.h"  // kNoDeadline
#include "hal/clock.h"

namespace pos {

class Timers {
 public:
  using Id = uint32_t;
  explicit Timers(hal::Clock& clock) : clock_(clock) {}

  // Runs `fn` once, `delayMs` from now.
  Id after(uint32_t delayMs, std::function<void()> fn) { return add(delayMs, 0, std::move(fn)); }
  // Runs `fn` every `periodMs`, first time `periodMs` from now. Never faster than once per minute for
  // anything that draws (docs/DISPLAY.md rule 4): that is the caller's job.
  Id every(uint32_t periodMs, std::function<void()> fn) { return add(periodMs, periodMs, std::move(fn)); }
  // True if the timer existed (not yet run, for one-shots).
  bool cancel(Id id);

  // Runs the timers that are due, earliest first (ties: in the order they were added). Timers added
  // by a callback wait for the next call, so `after(0, ...)` cannot spin. Returns how many ran.
  int runDue();
  // Time of the earliest timer, kNoDeadline if none.
  uint32_t nextDeadlineMs() const;
  size_t size() const { return timers_.size(); }

 private:
  struct Timer {
    Id id;
    uint64_t seq;
    uint32_t due;
    uint32_t period;  // 0 = one-shot
    std::function<void()> fn;
  };
  Id add(uint32_t delayMs, uint32_t period, std::function<void()> fn);

  hal::Clock& clock_;
  std::vector<Timer> timers_;
  Id nextId_ = 1;
  uint64_t nextSeq_ = 1;
};

}  // namespace pos
