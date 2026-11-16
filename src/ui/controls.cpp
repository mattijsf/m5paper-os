#include "ui/controls.h"

#include <algorithm>

#include "ui/root.h"

namespace pos::ui {

void strokeRound(Canvas& c, Rect r, int radius, int stroke, Color color) {
  for (int i = 0; i < stroke; ++i)
    c.drawRoundRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, std::max(0, radius - i), color);
}

// ---- Pressable

void Pressable::setPressed(bool p) {
  if (p == pressed_) return;
  pressed_ = p;
  invalidate(Hint::Feedback);
}

Pressable::~Pressable() { *alive_ = false; }

void Pressable::beginPress() {
  pos::Timers* timers = host() ? host()->timers() : nullptr;
  if (!timers) {
    setPressed(true);
    return;
  }
  const uint32_t seq = ++pressSeq_;
  timers->after(kPressDelayMs, [this, seq, alive = std::weak_ptr<bool>(alive_)] {
    auto a = alive.lock();
    if (a && *a && seq == pressSeq_) setPressed(true);
  });
}

bool Pressable::onTouch(const Gesture& g) {
  switch (g.type) {
    case Gesture::Down:
      if (enabled) beginPress();
      return true;
    case Gesture::Tap:
      cancelPress();
      setPressed(false);
      if (enabled) activate();  // last: the handler may rebuild the screen
      return true;
    case Gesture::LongPress:
      cancelPress();
      setPressed(false);
      if (enabled && onLongPress) onLongPress();
      return true;
    case Gesture::Cancel:
      cancelPress();
      setPressed(false);
      return true;
    default:
      return false;  // swipes belong to a parent (Pager)
  }
}

// ---- Button

Size Button::measure(int availW) {
  Canvas* c = canvas();
  const int tw = c ? measureText(*c, text_, Type::BodyStrong) : 0;
  return {std::min(availW, std::max(96, tw + 2 * kMargin)), kTouchMin};
}

void Button::draw(Canvas& c) {
  // A disabled primary button is drawn as an outline: disabled text on a disabled fill could not be read.
  const bool filled = enabled && (kind == Kind::Primary) != pressed_ && !(kind == Kind::Text && !pressed_);
  const Color fg = !enabled ? disabled : filled ? paper : ink;
  const Color back = filled ? ink : paper;
  if (filled) c.fillRoundRect(frame.x, frame.y, frame.w, frame.h, kRadius, enabled ? ink : disabled);
  if (!filled && (kind != Kind::Text || pressed_)) strokeRound(c, frame, kRadius, kStroke, enabled ? ink : disabled);
  const Color textBg = filled ? (enabled ? ink : disabled) : back;
  drawText(c, ellipsize(c, text_, Type::BodyStrong, frame.w - 2 * s2), frame.x + frame.w / 2, frame.y + frame.h / 2,
           Type::BodyStrong, fg, Anchor::MiddleCenter, textBg);
}

// ---- IconButton

void IconButton::draw(Canvas& c) {
  if (pressed_) c.fillRoundRect(frame.x, frame.y, frame.w, frame.h, kRadius, ink);
  drawIcon(c, icon, frame.x + frame.w / 2, frame.y + frame.h / 2, 32, pressed_ ? paper : (enabled ? ink : disabled),
           pressed_ ? ink : paper);
}

// ---- Toggle

void Toggle::setOn(bool on, Hint h) {
  if (on == on_) return;
  on_ = on;
  invalidate(h);
}

void Toggle::activate() {
  on_ = !on_;
  invalidate(Hint::Feedback);
  if (onChange) onChange(on_);
}

void Toggle::draw(Canvas& c) {
  const int x = frame.x + (frame.w - 88) / 2, y = frame.y + (frame.h - 48) / 2, cy = y + 24;
  const Rect pill{static_cast<int16_t>(x), static_cast<int16_t>(y), 88, 48};
  const Color body = enabled ? ink : disabled;
  if (on_) {
    c.fillRoundRect(x, y, 88, 48, 24, body);
    if (pressed_) strokeRound(c, Rect{static_cast<int16_t>(x + 3), static_cast<int16_t>(y + 3), 82, 42}, 21, 2, paper);
    c.fillCircle(x + 88 - 24, cy, 14, paper);
  } else {
    strokeRound(c, pill, 24, pressed_ ? 4 : kStroke, body);
    c.fillCircle(x + 24, cy, 12, body);
  }
}

// ---- Badge

std::string badgeText(int count) { return count > 99 ? "99+" : std::to_string(count); }

Size Badge::measure(int) {
  Canvas* c = canvas();
  const int tw = c ? measureText(*c, badgeText(count_), Type::Caption) : 0;
  return {std::max(28, tw + s2), 28};
}

int drawBadge(Canvas& c, int right, int cy, int count, Color bg) {
  const std::string t = badgeText(count);
  const int w = std::max(28, measureText(c, t, Type::Caption) + s2);
  c.fillRoundRect(right - w, cy - 14, w, 28, 14, ink);
  drawText(c, t, right - w / 2, cy, Type::Caption, paper, Anchor::MiddleCenter, ink);
  (void)bg;
  return w;
}

void Badge::draw(Canvas& c) { drawBadge(c, frame.x + frame.w, frame.y + frame.h / 2, count_, bg()); }

}  // namespace pos::ui
