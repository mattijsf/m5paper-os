#include "ui/panels.h"

#include <algorithm>

#include "ui/root.h"

namespace pos::ui {

// ---- Sheet

Sheet::Sheet() {
  transparent = true;  // draws over the screen it covers
  modal = true;
  setBg(paper);        // what its content sits on
  content_ = add<Column>(s2);
}

void Sheet::layout() {
  const int maxH = kScreenH * 6 / 10;
  const Size want = content_->measure(kContentW);
  const int h = std::min(maxH, want.h + 2 * kMargin + s1);
  panel_ = Rect{0, static_cast<int16_t>(kScreenH - h), kScreenW, static_cast<int16_t>(h)};
  frame = panel_;  // only the sheet is invalidated; touches outside reach it through `modal`
  content_->frame = Rect{kMargin, static_cast<int16_t>(panel_.y + kMargin + s1), kContentW, static_cast<int16_t>(h - 2 * kMargin - s1)};
  content_->layout();
}

void Sheet::draw(Canvas& c) {
  // Panel over the untouched screen: paper, a 2 px ink top edge, a grab handle.
  c.fillRect(panel_.x, panel_.y, panel_.w, panel_.h, paper);
  c.fillRect(panel_.x, panel_.y, panel_.w, kStroke, ink);
  c.fillRoundRect(kScreenW / 2 - 24, panel_.y + 12, 48, 4, 2, secondary);
}

bool Sheet::onTouch(const Gesture& g) {
  const bool outside = !contains(g.startX, g.startY);
  switch (g.type) {
    case Gesture::Tap:
      if (outside) close();
      return true;
    case Gesture::SwipeDown:
      close();
      return true;
    case Gesture::Down:
    case Gesture::LongPress:
    case Gesture::Cancel:
      return true;  // modal: nothing below the sheet gets touches
    default:
      return false;
  }
}

void Sheet::close() {
  if (onClose) onClose();
  if (Root* s = host()) s->closeOverlay(this);
}

// ---- Toast

Toast::Toast(std::string text) : text_(std::move(text)) {
  transparent = true;
  touchable = false;
  setBg(ink);
}

// A message, not a control: it must not look like a button. It did (an ink pill of the primary button's size at the bottom,
// often right on top of a button). Now it is an ink band over the status bar for
// its three seconds: edge to edge, no corners, regular weight, and it covers no content, no control and no keyboard.
void Toast::layout() { frame = Rect{0, 0, kScreenW, kStatusBarH}; }

void Toast::draw(Canvas& c) {
  c.fillRect(frame.x, frame.y, frame.w, frame.h, ink);
  drawText(c, ellipsize(c, text_, Type::Body, frame.w - 2 * kMargin), frame.x + frame.w / 2, frame.y + frame.h / 2, Type::Body, paper,
           Anchor::MiddleCenter, ink);
}

// ---- EmptyState

EmptyState::EmptyState(Icon icon, const std::string& title, const std::string& hint, const std::string& buttonText,
                       std::function<void()> onButton)
    : Column(s2, Align::Center) {
  add<Spacer>(0, 1);
  add<IconView>(icon, 64, secondary);
  auto* t = add<Label>(title, Type::Title);
  t->align = TextAlign::Center;
  if (!hint.empty()) {
    hint_ = add<Label>(hint, Type::Body, secondary);
    hint_->align = TextAlign::Center;
    hint_->maxLines = 3;
  }
  if (!buttonText.empty()) {
    add<Spacer>(s1);
    button_ = add<Button>(buttonText, Button::Kind::Secondary);
    button_->onClick = std::move(onButton);
  }
  add<Spacer>(0, 1);
}

}  // namespace pos::ui
