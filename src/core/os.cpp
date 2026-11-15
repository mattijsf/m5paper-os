#include "core/os.h"

#include "core/demo.h"
#include "core/log.h"

namespace pos {

bool Os::setup() {
  canvas_.setPsram(true);  // ignored by the simulator
  canvas_.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!canvas_.createSprite(kScreenW, kScreenH)) {
    POS_LOG("os", "canvas alloc failed");
    return false;
  }
  drawDemo(canvas_, p_.name);

  uint32_t t0 = p_.clock.millis();
  p_.display.push(canvas_, Rect{0, 0, kScreenW, kScreenH}, Refresh::Quality);
  p_.display.wait();
  POS_LOG("boot", "full quality refresh %ums", static_cast<unsigned>(p_.clock.millis() - t0));
  ready_ = true;
  return true;
}

void Os::loopOnce() {
  if (!ready_) return;
  hal::TouchEvent ev;
  while (p_.input.poll(ev)) {
    if (ev.type != hal::TouchEvent::Down) continue;
    uint32_t t0 = p_.clock.millis();
    Rect r = drawTouchMarker(canvas_, ev.x, ev.y);
    p_.display.push(canvas_, r, Refresh::Fast);
    p_.display.wait();
    POS_LOG("touch", "x=%d y=%d fast refresh %ums", static_cast<int>(ev.x), static_cast<int>(ev.y),
            static_cast<unsigned>(p_.clock.millis() - t0));
  }
}

}  // namespace pos
