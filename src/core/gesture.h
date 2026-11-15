// Gesture recogniser: raw touch events in, taps / long presses / swipes out (docs/UI.md "Gestures").
//
// For every touch the recogniser emits Down first, then exactly one of:
//   Tap          up within 600 ms, finger moved less than 24 px
//   LongPress    held 600 ms, moved less than 24 px (fires while the finger is still down)
//   Swipe*       moved more than 80 px along one axis, up within 800 ms (fires on up)
//   (nothing)    anything else
// and Cancel as soon as the finger has moved 24 px away (so a pressed widget can let go), also when
// the finger later swipes. After LongPress no tap follows.
#pragma once

#include <cstdint>
#include <vector>

#include "hal/input.h"

namespace pos {

struct Gesture {
  enum Type : uint8_t { Down, Tap, LongPress, SwipeLeft, SwipeRight, SwipeUp, SwipeDown, Cancel };
  Type type = Down;
  int16_t x = 0, y = 0;            // Down/Tap/LongPress: the touch point; Swipe/Cancel: where the finger is now
  int16_t startX = 0, startY = 0;  // where the finger went down
  uint32_t ms = 0;
};

struct GestureParams {
  uint32_t longPressMs = 600;  // also the tap limit
  int slopPx = 24;
  int swipePx = 80;
  uint32_t swipeMs = 800;
};

class GestureRecognizer {
 public:
  explicit GestureRecognizer(GestureParams p = {}) : p_(p) {}

  // Feed one touch event; gestures are appended to `out`.
  void feed(const hal::TouchEvent& ev, std::vector<Gesture>& out);
  // Call regularly (every loop pass) with the current time: fires LongPress.
  void tick(uint32_t nowMs, std::vector<Gesture>& out);
  // When tick() next has something to do (down + 600 ms), 0xFFFFFFFF if idle.
  uint32_t nextDeadlineMs() const;
  bool touching() const { return state_ != State::Idle; }

 private:
  enum class State : uint8_t { Idle, Down, Moved, LongPressed };
  Gesture make(Gesture::Type t, int x, int y, uint32_t ms) const;

  GestureParams p_;
  State state_ = State::Idle;
  int startX_ = 0, startY_ = 0;
  uint32_t startMs_ = 0;
};

}  // namespace pos
