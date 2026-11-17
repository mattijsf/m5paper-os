#include "ui/inputs.h"

#include <algorithm>

namespace pos::ui {

// ---- Segmented

void Segmented::setSelected(int i, Hint h) {
  i = std::max(0, std::min(i, static_cast<int>(options_.size()) - 1));
  if (i == selected_) return;
  selected_ = i;
  invalidate(h);
}

int Segmented::indexAt(int x) const {
  const int n = static_cast<int>(options_.size());
  if (n == 0) return -1;
  return std::max(0, std::min(n - 1, (x - frame.x) * n / std::max(1, static_cast<int>(frame.w))));
}

bool Segmented::onTouch(const Gesture& g) {
  switch (g.type) {
    case Gesture::Down:
      preview_ = indexAt(g.x);
      invalidate(Hint::Feedback);
      return true;
    case Gesture::Tap: {
      const int i = indexAt(g.startX);
      preview_ = -1;
      if (i < 0) return true;  // no options: nothing to select
      const bool changed = i != selected_;
      selected_ = i;
      invalidate(Hint::Feedback);
      if (changed && onChange) onChange(i);
      return true;
    }
    case Gesture::LongPress:
    case Gesture::Cancel:
      preview_ = -1;
      invalidate(Hint::Feedback);
      return true;
    default:
      return false;
  }
}

void Segmented::draw(Canvas& c) {
  const int n = static_cast<int>(options_.size());
  if (n == 0) return;
  const int shown = preview_ >= 0 ? preview_ : selected_;
  strokeRound(c, frame, kRadius, kStroke, ink);
  for (int i = 0; i < n; ++i) {
    const int x0 = frame.x + frame.w * i / n, x1 = frame.x + frame.w * (i + 1) / n;
    const bool sel = i == shown;
    if (sel) c.fillRoundRect(x0 + 4, frame.y + 4, x1 - x0 - 8, frame.h - 8, kRadius - 4, ink);
    else if (i > 0 && i - 1 != shown) c.drawFastVLine(x0, frame.y + 14, frame.h - 28, divider);
    drawText(c, ellipsize(c, options_[i], Type::BodyStrong, x1 - x0 - 2 * s1), (x0 + x1) / 2, frame.y + frame.h / 2, Type::BodyStrong,
             sel ? paper : ink, Anchor::MiddleCenter, sel ? ink : bg());
  }
}

// ---- Stepper

Stepper::Stepper(int value, int min, int max, int step, std::function<std::string(int)> format)
    : value_(std::max(min, std::min(value, max))), min_(min), max_(max), step_(step), format_(std::move(format)) {
  minus_ = add<IconButton>(Icon::Minus);
  plus_ = add<IconButton>(Icon::Plus);
  minus_->onClick = [this] { change(-step_); };
  plus_->onClick = [this] { change(step_); };
  minus_->enabled = value_ > min_;
  plus_->enabled = value_ < max_;
}

void Stepper::change(int delta) {
  const int before = value_;
  setValue(value_ + delta);
  if (value_ != before && onChange) onChange(value_);
}

void Stepper::setValue(int v, Hint h) {
  v = std::max(min_, std::min(v, max_));
  minus_->enabled = v > min_;
  plus_->enabled = v < max_;
  if (v == value_) return;
  value_ = v;
  invalidate(h);
}

void Stepper::layout() {
  minus_->frame = Rect{frame.x, frame.y, kTouchMin, frame.h};
  plus_->frame = Rect{static_cast<int16_t>(frame.x + frame.w - kTouchMin), frame.y, kTouchMin, frame.h};
}

void Stepper::draw(Canvas& c) {
  const std::string t = format_ ? format_(value_) : std::to_string(value_);
  drawText(c, t, frame.x + frame.w / 2, frame.y + frame.h / 2, Type::Title, ink, Anchor::MiddleCenter, bg());
}

// ---- LevelBar

void LevelBar::setPercent(int p, Hint h) {
  p = std::max(10, std::min(100, (p + 5) / 10 * 10));
  if (p == percent_) return;
  percent_ = p;
  invalidate(h);
}

int LevelBar::segmentAt(int x) const {
  return std::max(0, std::min(9, (x - frame.x) * 10 / std::max(1, static_cast<int>(frame.w))));
}

bool LevelBar::onTouch(const Gesture& g) {
  switch (g.type) {
    case Gesture::Down:
      preview_ = (segmentAt(g.x) + 1) * 10;
      invalidate(Hint::Feedback);
      return true;
    case Gesture::Tap: {
      const int p = (segmentAt(g.startX) + 1) * 10;
      preview_ = 0;
      const bool changed = p != percent_;
      percent_ = p;
      invalidate(Hint::Feedback);
      if (changed && onChange) onChange(p);
      return true;
    }
    case Gesture::LongPress:
    case Gesture::Cancel:
      preview_ = 0;
      invalidate(Hint::Feedback);
      return true;
    default:
      return false;
  }
}

void LevelBar::draw(Canvas& c) {
  const int shown = preview_ ? preview_ : percent_;
  const int gap = 6, h = 48;
  const int segW = (frame.w - 9 * gap) / 10;
  const int x0 = frame.x + (frame.w - (10 * segW + 9 * gap)) / 2, y = frame.y + (frame.h - h) / 2;
  for (int i = 0; i < 10; ++i) {
    const int x = x0 + i * (segW + gap);
    if ((i + 1) * 10 <= shown) c.fillRoundRect(x, y, segW, h, kRadiusSmall, ink);
    else strokeRound(c, Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(segW), static_cast<int16_t>(h)}, kRadiusSmall, kStroke, ink);
  }
}

}  // namespace pos::ui
