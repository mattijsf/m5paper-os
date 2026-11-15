// Overlays and panels: Sheet (modal bottom sheet), Toast (timed one-liner), EmptyState.
// docs/UI.md "Widgets". Overlays are added with Screen::addOverlay / showToast.
#pragma once

#include <functional>
#include <string>

#include "ui/controls.h"
#include "ui/layout.h"

namespace pos::ui {

// Bottom sheet drawn over the untouched screen with a 2 px ink top edge, at most 60% of the height.
// Modal: a tap anywhere outside it closes it and does not reach the widgets below; swipe down closes too. Put the controls into `content()` (a Column with 24 px margins).
// Open with Screen::addOverlay(std::make_unique<Sheet>()); close() restores what was under it.
class Sheet : public Widget {
 public:
  Sheet();
  std::function<void()> onClose;
  Column* content() const { return content_; }
  // Closes this sheet (calls onClose first). The Sheet is destroyed after the current gesture.
  void close();

  void layout() override;
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;
  Rect panel() const { return panel_; }

 private:
  Column* content_;
  Rect panel_;
};

// One line at the bottom, ink pill with paper text. Created by Screen::showToast; touches pass through.
class Toast : public Widget {
 public:
  explicit Toast(std::string text);
  void layout() override;
  void draw(Canvas& c) override;

 private:
  std::string text_;
};

// Centered column: 64 px icon, title, hint (up to 3 lines) and an optional button. For empty lists,
// errors and "Loading...".
class EmptyState : public Column {
 public:
  EmptyState(Icon icon, const std::string& title, const std::string& hint = "", const std::string& buttonText = "",
             std::function<void()> onButton = {});
  Button* button() const { return button_; }

 private:
  Button* button_ = nullptr;
};

}  // namespace pos::ui
