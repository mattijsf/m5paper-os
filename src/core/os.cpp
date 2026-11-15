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
  if (screen_ == "fonts") drawFontSheet(canvas_);
  else drawDemo(canvas_, p_.name);

  uint32_t t0 = p_.clock.millis();
  dm_.forceClean();  // first frame: Quality over the whole screen
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
    POS_LOG("touch", "x=%d y=%d t=%u", static_cast<int>(ev.x), static_cast<int>(ev.y),
            static_cast<unsigned>(p_.clock.millis()));
    Rect r = drawTouchMarker(canvas_, ev.x, ev.y);
    dm_.invalidate(r, Hint::Feedback);
  }
  if (dm_.dirty()) dm_.commit();
  dm_.tick(p_.clock.millis());
}

std::vector<uint8_t> Os::frame4() {
  std::vector<uint8_t> out;
  if (p_.display.snapshot4(out)) return out;
  return dm_.shadowPacked();
}

}  // namespace pos
