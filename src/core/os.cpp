#include "core/os.h"

#include <algorithm>
#include <chrono>
#include <thread>

#include "core/demo.h"
#include "core/log.h"
#include "hal/system.h"
#include "core/timefmt.h"

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

void Os::loadConfig() {
  config_.load();
  secrets_.load();
  net_.load();
  applyDisplayConfig();
  applyTimezone(config_.net.timezone);
}

void Os::applyDisplayConfig() {
  DisplayParams p;
  p.ghostLimit = config_.display.ghostLimit;
  p.settleMs = static_cast<uint32_t>(config_.display.settleMs);
  p.idleCleanMs = static_cast<uint32_t>(config_.display.idleCleanMs);
  dm_.setParams(p);
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
    lastInputMs_ = p_.clock.millis();
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
  net_.tick();
  // 4. draw what changed (widgets mark themselves dirty), then commit what was invalidated
  if (surface_) {
    const uint32_t t0 = p_.clock.millis();
    surface_->render(canvas_, dm_);
    const uint32_t dt = p_.clock.millis() - t0;
    if (dt >= 30) POS_LOG("perf", "render took %ums", static_cast<unsigned>(dt));  // layout + drawing of a screen change
  }
  if (dm_.dirty()) dm_.commit();
  dm_.tick(p_.clock.millis());
}

uint32_t Os::msToNextDeadline() {
  const uint32_t now = p_.clock.millis();
  uint32_t best = kNoDeadline;
  for (uint32_t deadline : {dm_.nextDeadlineMs(), timers_.nextDeadlineMs(), gestures_.nextDeadlineMs(), net_.msToNextDeadline() == kNoDeadline ? kNoDeadline : now + net_.msToNextDeadline()}) {
    if (deadline == kNoDeadline) continue;
    int32_t d = static_cast<int32_t>(deadline - now);
    best = std::min<uint32_t>(best == kNoDeadline ? 0xFFFFFFFEu : best, d < 0 ? 0 : static_cast<uint32_t>(d));
  }
  return best;
}

uint32_t Os::idleWaitMs() {
  constexpr uint32_t kMaxIdleMs = 1000;
  if (dm_.dirty() || worker_.hasCompleted() || (surface_ && surface_->dirty())) return 0;
  const uint32_t next = msToNextDeadline();
  return next == kNoDeadline ? kMaxIdleMs : std::min<uint32_t>(kMaxIdleMs, next);
}

std::string Os::statusText() {
  std::string out;
  out += "keys:";
  for (const auto& k : secrets_.keys()) out += " " + k + "(" + std::to_string(secrets_.valueLength(k)) + ")";
  if (secrets_.keys().empty()) out += secrets_.fileFound() ? " (none)" : " (no paperos_config)";
  out += "\n";
  const auto known = net_.known();
  out += "networks: " + std::to_string(known.size()) + " known";
  for (const auto& c : known) out += std::string(c.fromCard ? " [card] " : " [device] ") + c.ssid;
  out += "\n";
  hal::WifiLink l = p_.net.link();
  if (l.connected) {
    out += "wifi: connected ssid=" + l.ssid + " ip=" + l.ip + " rssi=" + std::to_string(l.rssi) + " ch=" + std::to_string(l.channel) + "\n";
  } else {
    out += "wifi: off\n";
  }
  NetService::Result r = net_.lastResult();
  if (!r.ssid.empty() || !r.error.empty())
    out += std::string("last connect: ") + (r.ok ? "ok" : "failed") + " ssid=" + r.ssid + (r.fast ? " fast" : "") + " " + std::to_string(r.ms) + "ms" + (r.error.empty() ? "" : " (" + r.error + ")") + "\n";
  if (!r.scan.empty()) out += "scan: " + r.scan + "\n";
  hal::MemoryInfo m = hal::memoryInfo();
  out += "heap: " + std::to_string(m.heapFree) + "/" + std::to_string(m.heapTotal) + " psram: " + std::to_string(m.psramFree) + "/" + std::to_string(m.psramTotal) + "\n";
  {  // Does any secret value (first 8 characters) appear in the log ring or in this very output? Counts only.
    const std::string log = logTail(50);
    int checked = 0, hits = 0;
    for (const auto& k : secrets_.keys()) {
      const std::string v = secrets_.get(k);
      if (v.size() < 8) continue;
      ++checked;
      const std::string prefix = v.substr(0, 8);
      for (size_t at = log.find(prefix); at != std::string::npos; at = log.find(prefix, at + 1)) ++hits;
      for (size_t at = out.find(prefix); at != std::string::npos; at = out.find(prefix, at + 1)) ++hits;
    }
    out += "secret leak check: " + std::to_string(checked) + " values checked, " + std::to_string(hits) + " occurrences in the log and in this output\n";
  }
  out += std::string("clock: ") + (p_.clock.now() > 0 ? "set" : "not set") + "\n";
  return out;
}

std::vector<uint8_t> Os::frame4() {
  std::vector<uint8_t> out;
  if (p_.display.snapshot4(out)) return out;
  return dm_.shadowPacked();
}

}  // namespace pos
