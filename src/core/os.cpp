#include "core/os.h"

#include <algorithm>
#include <chrono>
#include <thread>

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
  if (surface_) surface_->render(canvas_, dm_);
  else if (screen_ == "fonts") drawFontSheet(canvas_);
  else canvas_.fillScreen(kWhite);

  uint32_t t0 = p_.clock.millis();
  worker_.start();
  dm_.forceClean();  // first frame: Quality over the whole screen
  p_.display.wait();
  POS_LOG("boot", "full quality refresh %ums", static_cast<unsigned>(p_.clock.millis() - t0));
  ready_ = true;
  return true;
}

void Os::setSurface(std::unique_ptr<Surface> s) {
  surface_ = std::move(s);
  if (!ready_) return;
  gestures_ = GestureRecognizer();
  if (surface_) surface_->render(canvas_, dm_);
  else canvas_.fillScreen(kWhite);
  dm_.forceClean();
}

void Os::loopOnce() {
  if (!ready_) return;
  // 1. input
  hal::TouchEvent ev;
  std::vector<Gesture> gs;
  while (p_.input.poll(ev)) {
    if (surface_) {
      if (ev.type == hal::TouchEvent::Down)
        POS_LOG("touch", "x=%d y=%d t=%u", static_cast<int>(ev.x), static_cast<int>(ev.y), static_cast<unsigned>(p_.clock.millis()));
      gestures_.feed(ev, gs);
      continue;
    }
    if (ev.type != hal::TouchEvent::Down) continue;
    POS_LOG("touch", "x=%d y=%d t=%u", static_cast<int>(ev.x), static_cast<int>(ev.y),
            static_cast<unsigned>(p_.clock.millis()));
    Rect r = drawTouchMarker(canvas_, ev.x, ev.y);  // no Surface: just mark the touch
    dm_.invalidate(r, Hint::Feedback);
  }
  if (surface_) {
    gestures_.tick(p_.clock.millis(), gs);
    for (const Gesture& g : gs) surface_->onGesture(g);
  }
  // 2. finished worker jobs, 3. due timers
  worker_.runCompleted();
  timers_.runDue();
  // 4. draw what changed (widgets mark themselves dirty), then commit what was invalidated
  if (surface_) surface_->render(canvas_, dm_);
  if (dm_.dirty()) dm_.commit();
  dm_.tick(p_.clock.millis());
}

uint32_t Os::idleWaitMs() {
  constexpr uint32_t kMaxIdleMs = 1000;
  if (dm_.dirty() || worker_.hasCompleted() || (surface_ && surface_->dirty())) return 0;
  const uint32_t now = p_.clock.millis();
  uint32_t wait = kMaxIdleMs;
  for (uint32_t deadline : {dm_.nextDeadlineMs(), timers_.nextDeadlineMs(), gestures_.nextDeadlineMs()}) {
    if (deadline == kNoDeadline) continue;
    int32_t d = static_cast<int32_t>(deadline - now);
    wait = std::min<uint32_t>(wait, d < 0 ? 0 : static_cast<uint32_t>(d));
  }
  return wait;
}

std::vector<uint8_t> Os::frame4() {
  std::vector<uint8_t> out;
  if (p_.display.snapshot4(out)) return out;
  return dm_.shadowPacked();
}

}  // namespace pos
