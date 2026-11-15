#include "core/gesture.h"

#include <cstdlib>

namespace pos {

Gesture GestureRecognizer::make(Gesture::Type t, int x, int y, uint32_t ms) const {
  Gesture g;
  g.type = t;
  g.x = static_cast<int16_t>(x);
  g.y = static_cast<int16_t>(y);
  g.startX = static_cast<int16_t>(startX_);
  g.startY = static_cast<int16_t>(startY_);
  g.ms = ms;
  return g;
}

void GestureRecognizer::feed(const hal::TouchEvent& ev, std::vector<Gesture>& out) {
  const int dx = ev.x - startX_, dy = ev.y - startY_;
  switch (ev.type) {
    case hal::TouchEvent::Down:
      if (state_ != State::Idle) out.push_back(make(Gesture::Cancel, ev.x, ev.y, ev.ms));  // lost an Up
      state_ = State::Down;
      startX_ = ev.x;
      startY_ = ev.y;
      startMs_ = ev.ms;
      out.push_back(make(Gesture::Down, ev.x, ev.y, ev.ms));
      break;

    case hal::TouchEvent::Move:
      if (state_ == State::Down && dx * dx + dy * dy >= p_.slopPx * p_.slopPx) {
        state_ = State::Moved;
        out.push_back(make(Gesture::Cancel, ev.x, ev.y, ev.ms));
      }
      break;

    case hal::TouchEvent::Up: {
      const State was = state_;
      state_ = State::Idle;
      const uint32_t held = ev.ms - startMs_;
      if (was == State::Down) {
        if (dx * dx + dy * dy >= p_.slopPx * p_.slopPx) {
          // Up far from Down without a Move (a jump): same as moved.
          out.push_back(make(Gesture::Cancel, ev.x, ev.y, ev.ms));
        } else if (held >= p_.longPressMs) {
          out.push_back(make(Gesture::LongPress, startX_, startY_, ev.ms));  // tick() was late
          break;
        } else {
          out.push_back(make(Gesture::Tap, startX_, startY_, ev.ms));
          break;
        }
      } else if (was != State::Moved) {
        break;  // LongPressed or Idle: nothing follows
      }
      // Moved: a swipe if far enough along one axis, fast enough.
      if (held <= p_.swipeMs) {
        const int ax = std::abs(dx), ay = std::abs(dy);
        if (ax > p_.swipePx && ax > ay) out.push_back(make(dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight, ev.x, ev.y, ev.ms));
        else if (ay > p_.swipePx && ay > ax) out.push_back(make(dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown, ev.x, ev.y, ev.ms));
      }
      break;
    }
  }
}

void GestureRecognizer::tick(uint32_t nowMs, std::vector<Gesture>& out) {
  if (state_ == State::Down && nowMs - startMs_ >= p_.longPressMs) {
    state_ = State::LongPressed;
    out.push_back(make(Gesture::LongPress, startX_, startY_, nowMs));
  }
}

uint32_t GestureRecognizer::nextDeadlineMs() const {
  return state_ == State::Down ? startMs_ + p_.longPressMs : 0xFFFFFFFFu;
}

}  // namespace pos
