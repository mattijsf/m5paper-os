#include <Arduino.h>
#include <M5Unified.h>

#include "platform/device/device.h"

namespace pos::device {

void Input::update() {
  auto t = M5.Touch.getDetail();
  auto add = [&](hal::TouchEvent::Type type) {
    hal::TouchEvent ev;
    ev.type = type;
    ev.x = t.x;
    ev.y = t.y;
    ev.ms = ::millis();
    inject(ev);
  };
  if (t.wasPressed()) add(hal::TouchEvent::Down);
  else if (t.isPressed() && (t.deltaX() != 0 || t.deltaY() != 0)) add(hal::TouchEvent::Move);
  if (t.wasReleased()) add(hal::TouchEvent::Up);
}

void Input::inject(const hal::TouchEvent& ev) {
  if (queue_.size() < 64) queue_.push_back(ev);
}

bool Input::poll(hal::TouchEvent& out) {
  if (queue_.empty()) return false;
  out = queue_.front();
  queue_.pop_front();
  return true;
}

}  // namespace pos::device
