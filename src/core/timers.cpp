#include "core/timers.h"

namespace pos {

Timers::Id Timers::add(uint32_t delayMs, uint32_t period, std::function<void()> fn) {
  Timer t{nextId_++, nextSeq_++, clock_.millis() + delayMs, period, std::move(fn)};
  timers_.push_back(std::move(t));
  return timers_.back().id;
}

bool Timers::cancel(Id id) {
  for (size_t i = 0; i < timers_.size(); ++i) {
    if (timers_[i].id == id) {
      timers_.erase(timers_.begin() + i);
      return true;
    }
  }
  return false;
}

int Timers::runDue() {
  const uint32_t now = clock_.millis();
  const uint64_t limit = nextSeq_;  // timers added during this call have seq >= limit
  int ran = 0;
  for (;;) {
    // Earliest due timer that existed when we started: by due time, then by seq.
    int best = -1;
    for (size_t i = 0; i < timers_.size(); ++i) {
      const Timer& t = timers_[i];
      if (t.seq >= limit || static_cast<int32_t>(now - t.due) < 0) continue;
      if (best < 0 || static_cast<int32_t>(t.due - timers_[best].due) < 0 ||
          (t.due == timers_[best].due && t.seq < timers_[best].seq))
        best = static_cast<int>(i);
    }
    if (best < 0) break;
    Timer t = std::move(timers_[best]);
    timers_.erase(timers_.begin() + best);
    if (t.period) {  // re-arm before running, so the callback can cancel itself
      Timer again{t.id, nextSeq_++, t.due + t.period, t.period, t.fn};
      if (static_cast<int32_t>(now - again.due) >= 0) again.due = now + t.period;  // do not catch up
      timers_.push_back(std::move(again));
    }
    t.fn();
    ++ran;
  }
  return ran;
}

uint32_t Timers::nextDeadlineMs() const {
  uint32_t next = kNoDeadline;
  for (const Timer& t : timers_)
    if (next == kNoDeadline || static_cast<int32_t>(t.due - next) < 0) next = t.due;
  return next;
}

}  // namespace pos
