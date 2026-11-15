#include "ui/chrome.h"

#include <algorithm>

namespace pos::ui {

// ---- StatusBar

void StatusBar::setTime(const std::string& t) { if (t != time_) { time_ = t; invalidate(Hint::Content); } }
void StatusBar::setBattery(int p, bool charging) {
  if (p != percent_ || charging != charging_) { percent_ = p; charging_ = charging; invalidate(Hint::Content); }
}
void StatusBar::setWifi(bool on) { if (on != wifi_) { wifi_ = on; invalidate(Hint::Content); } }

void StatusBar::draw(Canvas& c) {
  const int cy = frame.y + (frame.h - 1) / 2;
  drawText(c, time_, frame.x + kMargin, cy, Type::Caption, ink, Anchor::MiddleLeft, bg());
  int right = frame.x + frame.w - kMargin;
  if (percent_ >= 0) {
    const std::string p = std::to_string(percent_) + "%";
    drawText(c, p, right, cy, Type::Caption, ink, Anchor::MiddleRight, bg());
    right -= measureText(c, p, Type::Caption) + s1;
    const Icon bat = charging_ ? Icon::BatteryCharging : percent_ < 15 ? Icon::BatteryLow : Icon::Battery;
    drawIcon(c, bat, right - 16, cy, 32, ink, bg());
    right -= 32 + s2;
  }
  drawIcon(c, wifi_ ? Icon::Wifi : Icon::WifiOff, right - 16, cy, 32, wifi_ ? ink : disabled, bg());
  c.drawFastHLine(frame.x, frame.y + frame.h - 1, frame.w, divider);
}

// ---- TitleBar

TitleBar::TitleBar() {
  back_ = add<IconButton>(Icon::ArrowLeft);
  title_ = add<Label>("", Type::Title);
  action_ = add<IconButton>(Icon::Settings);
  action_->visible = false;
  back_->onClick = [this] { if (onBack) onBack(); };
  back_->onLongPress = [this] { if (onHome) onHome(); };
}

void TitleBar::setTitle(const std::string& t) { title_->setText(t); }

void TitleBar::setAction(Icon icon, std::function<void()> fn) {
  action_->icon = icon;
  action_->onClick = std::move(fn);
  action_->visible = true;
  layout();
  invalidate(Hint::Content);
}

void TitleBar::clearAction() {
  if (!action_->visible) return;
  action_->visible = false;
  action_->onClick = nullptr;
  layout();
  invalidate(Hint::Content);
}

void TitleBar::layout() {
  back_->frame = Rect{frame.x, frame.y, kTouchMin, frame.h};
  const int right = action_->visible ? frame.x + frame.w - kTouchMin : frame.x + frame.w - kMargin;
  action_->frame = Rect{static_cast<int16_t>(frame.x + frame.w - kTouchMin), frame.y, kTouchMin, frame.h};
  const int tx = frame.x + kTouchMin;
  const Size s = title_->measure(right - tx);
  title_->frame = Rect{static_cast<int16_t>(tx), static_cast<int16_t>(frame.y + (frame.h - s.h) / 2), static_cast<int16_t>(right - tx),
                       static_cast<int16_t>(s.h)};
}

// ---- TabBar

void TabBar::setSelected(int i) {
  i = std::max(0, std::min(i, static_cast<int>(tabs_.size()) - 1));
  if (i == selected_) return;
  selected_ = i;
  invalidate(Hint::Feedback);
}

int TabBar::indexAt(int x) const {
  const int n = static_cast<int>(tabs_.size());
  return n == 0 ? -1 : std::max(0, std::min(n - 1, (x - frame.x) * n / std::max(1, static_cast<int>(frame.w))));
}

bool TabBar::onTouch(const Gesture& g) {
  switch (g.type) {
    case Gesture::Down:
      pressed_ = indexAt(g.x);
      invalidate(Hint::Feedback);
      return true;
    case Gesture::Tap: {
      const int i = indexAt(g.startX);
      pressed_ = -1;
      const bool changed = i != selected_;
      selected_ = i;
      invalidate(Hint::Feedback);
      if (changed && onSelect) onSelect(i);
      return true;
    }
    case Gesture::LongPress:
    case Gesture::Cancel:
      pressed_ = -1;
      invalidate(Hint::Feedback);
      return true;
    default:
      return false;
  }
}

void TabBar::draw(Canvas& c) {
  const int n = static_cast<int>(tabs_.size());
  if (n == 0) return;
  c.drawFastHLine(frame.x, frame.y, frame.w, divider);
  const int shown = pressed_ >= 0 ? pressed_ : selected_;
  for (int i = 0; i < n; ++i) {
    const int x0 = frame.x + frame.w * i / n, x1 = frame.x + frame.w * (i + 1) / n, cx = (x0 + x1) / 2;
    const bool sel = i == shown;
    if (sel) c.fillRoundRect(x0 + s1, frame.y + s1, x1 - x0 - 2 * s1, frame.h - 2 * s1, kRadius, ink);
    const Color fg = sel ? paper : ink, back = sel ? ink : bg();
    drawIcon(c, tabs_[i].icon, cx, frame.y + 34, 32, fg, back);
    drawText(c, tabs_[i].label, cx, frame.y + 72, Type::Caption, fg, Anchor::MiddleCenter, back);
  }
}

}  // namespace pos::ui
