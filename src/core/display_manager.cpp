#include "core/display_manager.h"

#include <algorithm>
#include <cstdio>

#include "core/log.h"

namespace pos {

namespace {

constexpr size_t kLogMax = 200;

Rect clip(Rect r) {
  int x0 = std::max<int>(r.x, 0), y0 = std::max<int>(r.y, 0);
  int x1 = std::min<int>(r.x + r.w, kScreenW), y1 = std::min<int>(r.y + r.h, kScreenH);
  if (x1 <= x0 || y1 <= y0) return Rect{};
  return Rect{static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0),
              static_cast<int16_t>(y1 - y0)};
}

Rect unite(Rect a, Rect b) {
  if (a.empty()) return b;
  if (b.empty()) return a;
  int x0 = std::min<int>(a.x, b.x), y0 = std::min<int>(a.y, b.y);
  int x1 = std::max<int>(a.x + a.w, b.x + b.w), y1 = std::max<int>(a.y + a.h, b.y + b.h);
  return Rect{static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0),
              static_cast<int16_t>(y1 - y0)};
}

bool touches(Rect a, Rect b) {
  return a.x <= b.x + b.w && b.x <= a.x + a.w && a.y <= b.y + b.h && b.y <= a.y + a.h;
}

constexpr float kScreenPixels = static_cast<float>(kScreenW) * kScreenH;

}  // namespace

DisplayManager::DisplayManager(hal::Display& display, Canvas& canvas, hal::Clock& clock, DisplayParams params)
    : display_(display), canvas_(canvas), clock_(clock), params_(params),
      shadow_(static_cast<size_t>(kScreenW) * kScreenH / 2, 0xFF) {}

uint8_t DisplayManager::level(int x, int y) const {
  const auto* src = static_cast<const uint8_t*>(canvas_.getBuffer());
  return static_cast<uint8_t>((src[static_cast<size_t>(y) * kScreenW + x] + 8) / 17);
}

uint8_t DisplayManager::shadowAt(size_t i) const {
  uint8_t b = shadow_[i / 2];
  return (i & 1) ? (b & 15) : (b >> 4);
}

void DisplayManager::invalidate(Rect r, Hint hint) {
  r = clip(r);
  if (r.empty()) return;
  for (auto& p : pending_) {
    if (p.hint == hint) {  // one pending rect per hint: keeps the list short
      p.rect = unite(p.rect, r);
      return;
    }
  }
  pending_.push_back({r, hint});
}

Rect DisplayManager::diff(Rect r, uint32_t& changed, bool& onlyBlackWhite) const {
  const auto* src = static_cast<const uint8_t*>(canvas_.getBuffer());
  int minX = r.x + r.w, minY = r.y + r.h, maxX = -1, maxY = -1;
  changed = 0;
  onlyBlackWhite = true;
  for (int y = r.y; y < r.y + r.h; ++y) {
    const uint8_t* row = src + static_cast<size_t>(y) * kScreenW;
    size_t base = static_cast<size_t>(y) * kScreenW;
    for (int x = r.x; x < r.x + r.w; ++x) {
      uint8_t q = static_cast<uint8_t>((row[x] + 8) / 17);
      if (shadowValid_ && q == shadowAt(base + x)) continue;
      ++changed;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
      if (y < minY) minY = y;
      if (y > maxY) maxY = y;
      if (q != 0 && q != 15) onlyBlackWhite = false;
    }
  }
  if (changed == 0) return Rect{};
  return Rect{static_cast<int16_t>(minX), static_cast<int16_t>(minY), static_cast<int16_t>(maxX - minX + 1),
              static_cast<int16_t>(maxY - minY + 1)};
}

void DisplayManager::updateShadow(Rect r) {
  for (int y = r.y; y < r.y + r.h; ++y) {
    size_t base = static_cast<size_t>(y) * kScreenW;
    for (int x = r.x; x < r.x + r.w; ++x) {
      size_t i = base + x;
      uint8_t q = level(x, y);
      uint8_t& b = shadow_[i / 2];
      b = (i & 1) ? static_cast<uint8_t>((b & 0xF0) | q) : static_cast<uint8_t>((b & 0x0F) | (q << 4));
    }
  }
}

void DisplayManager::push(Rect r, Refresh mode, uint32_t changed, const char* why) {
  uint32_t now = clock_.millis();
  display_.push(canvas_, r, mode);
  RefreshEntry e;
  e.t = now;
  e.mode = mode == Refresh::Quality ? 'Q' : mode == Refresh::Text ? 'T' : 'F';
  e.rect = r;
  e.changed = changed;
  e.why = why;
  log_.push_back(e);
  if (log_.size() > kLogMax) log_.erase(log_.begin(), log_.begin() + (log_.size() - kLogMax));
  lastActivityMs_ = now;
  (mode == Refresh::Quality ? counts_.quality : mode == Refresh::Text ? counts_.text : counts_.fast)++;
  POS_LOG("disp", "push %c rect=%d,%d,%d,%d changed=%u why=%s", e.mode, r.x, r.y, r.w, r.h,
          static_cast<unsigned>(changed), why);
}

void DisplayManager::addSettle(Rect r, float added, uint32_t nowMs) {
  // A new Fast pass touching a pending settle region merges with it and restarts the timer.
  Settle s{r, nowMs + params_.settleMs, added};
  for (size_t i = 0; i < settles_.size();) {
    if (touches(settles_[i].rect, s.rect)) {
      s.rect = unite(s.rect, settles_[i].rect);
      s.added += settles_[i].added;
      settles_.erase(settles_.begin() + i);
    } else {
      ++i;
    }
  }
  settles_.push_back(s);
}

void DisplayManager::pushPass(Rect r, Hint hint) {
  uint32_t changed = 0;
  bool bw = true;
  Rect box = diff(r, changed, bw);
  if (box.empty()) return;
  const uint32_t now = clock_.millis();
  const float added = static_cast<float>(changed) / kScreenPixels;

  switch (hint) {
    case Hint::Feedback:
      push(box, Refresh::Fast, changed, "feedback");
      score_ += added;
      updateShadow(box);
      if (!bw) addSettle(box, added, now);
      break;
    case Hint::Content:
      push(box, Refresh::Fast, changed, "content");
      score_ += added;
      updateShadow(box);
      addSettle(box, added, now);
      break;
    case Hint::Screen:
      if (score_ >= params_.ghostLimit) {
        // Navigation hides the clean: one Quality flash over the whole screen.
        Rect full{0, 0, kScreenW, kScreenH};
        push(full, Refresh::Quality, changed, "screen");
        updateShadow(full);
        shadowValid_ = true;
        score_ = 0;
        settles_.clear();
      } else {
        push(box, Refresh::Fast, changed, "screen");
        score_ += added;
        updateShadow(box);
        addSettle(box, added, now);
      }
      break;
    case Hint::Image:
      push(box, Refresh::Quality, changed, "image");
      updateShadow(box);
      // The image area no longer needs a settle pass.
      for (size_t i = 0; i < settles_.size();) {
        if (touches(settles_[i].rect, box)) settles_.erase(settles_.begin() + i);
        else ++i;
      }
      break;
  }
}

void DisplayManager::commit() {
  if (pending_.empty()) return;
  const bool busy = display_.busy();
  // Priority order. Later rects diff against the shadow updated by earlier ones. There is at most
  // one pending rect per hint (invalidate() merges them).
  static const Hint order[] = {Hint::Feedback, Hint::Content, Hint::Screen, Hint::Image};
  for (Hint h : order) {
    if (busy && h != Hint::Feedback) continue;  // coalesce: wait for the panel
    for (size_t i = 0; i < pending_.size(); ++i) {
      if (pending_[i].hint != h) continue;
      Rect r = pending_[i].rect;
      pending_.erase(pending_.begin() + i);
      pushPass(r, h);
      break;
    }
  }
}

void DisplayManager::forceClean() {
  Rect full{0, 0, kScreenW, kScreenH};
  push(full, Refresh::Quality, static_cast<uint32_t>(kScreenW) * kScreenH, shadowValid_ ? "clean" : "boot");
  updateShadow(full);
  shadowValid_ = true;
  score_ = 0;
  settles_.clear();
  pending_.clear();
}

void DisplayManager::tick(uint32_t nowMs) {
  // Settle passes: Fast regions that stayed quiet get their 16 grays back.
  if (!display_.busy()) {
    for (size_t i = 0; i < settles_.size(); ++i) {
      if (static_cast<int32_t>(nowMs - settles_[i].dueMs) < 0) continue;
      Settle s = settles_[i];
      settles_.erase(settles_.begin() + i);
      push(s.rect, Refresh::Text, static_cast<uint32_t>(s.rect.w) * s.rect.h, "settle");
      score_ = std::max(0.0f, score_ - s.added / 2);
      break;  // one per tick: the panel is busy afterwards
    }
    if (score_ >= 2 * params_.ghostLimit && settles_.empty() && pending_.empty() &&
        static_cast<int32_t>(nowMs - lastActivityMs_) >= static_cast<int32_t>(params_.idleCleanMs)) {
      forceClean();
    }
  }
  // Completion of the previous push, for latency numbers in the log.
  bool busy = display_.busy();
  if (!busy && wasBusy_) POS_LOG("disp", "panel idle %ums after last push", static_cast<unsigned>(nowMs - lastActivityMs_));
  wasBusy_ = busy;
}

uint32_t DisplayManager::nextDeadlineMs() const {
  if (!pending_.empty()) return 0;  // waiting only for the panel: due now
  uint32_t next = kNoDeadline;
  for (const auto& s : settles_) {
    if (next == kNoDeadline || static_cast<int32_t>(s.dueMs - next) < 0) next = s.dueMs;
  }
  if (score_ >= 2 * params_.ghostLimit) {
    uint32_t t = lastActivityMs_ + params_.idleCleanMs;
    if (next == kNoDeadline || static_cast<int32_t>(t - next) < 0) next = t;
  }
  return next;
}

std::string DisplayManager::refreshLogTail(int maxLines) const {
  std::string out;
  size_t n = log_.size();
  size_t from = (maxLines > 0 && static_cast<size_t>(maxLines) < n) ? n - maxLines : 0;
  for (size_t i = from; i < n; ++i) {
    const RefreshEntry& e = log_[i];
    char buf[120];
    std::snprintf(buf, sizeof buf, "t=%u mode=%c rect=%d,%d,%d,%d changed=%u why=%s\n", static_cast<unsigned>(e.t),
                  e.mode, e.rect.x, e.rect.y, e.rect.w, e.rect.h, static_cast<unsigned>(e.changed), e.why);
    out += buf;
  }
  return out;
}

}  // namespace pos
