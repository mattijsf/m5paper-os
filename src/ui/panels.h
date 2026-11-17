// Overlays and panels: Sheet (modal bottom sheet), Toast (timed one-liner), EmptyState.
// docs/UI.md "Widgets". Overlays are added with Root::addOverlay / showToast.
#pragma once

#include <functional>
#include <string>

#include "ui/controls.h"
#include "ui/layout.h"

namespace pos::ui {

// Bottom sheet drawn over the untouched screen with a 2 px ink top edge, at most 60% of the height.
// Modal: a tap anywhere outside it closes it and does not reach the widgets below; swipe down closes too. Put the controls
// into `content()` (a Column with 24 px margins). Open with openSheet() or confirmSheet(); close() restores what was under it.
class Sheet : public Widget {
 public:
  Sheet();
  std::function<void()> onClose;
  Column* content() const { return content_; }
  // Closes this sheet (calls onClose first). The Sheet is destroyed after the current gesture.
  void close();
  // A handler that closes this sheet and then runs `action` (if any): what a button of the sheet does.
  std::function<void()> closing(std::function<void()> action = {});
  // A button at the end of the content that closes the sheet, then runs `action`: "Cancel", "Done", an answer.
  Button* addButton(const std::string& text, std::function<void()> action = {}, Button::Kind kind = Button::Kind::Primary);

  void layout() override;
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;
  Rect panel() const { return panel_; }

 private:
  Column* content_;
  Rect panel_;
};

// Opens a sheet over the screen `from` is on; `fill` puts its controls in. The sheet belongs to that screen's Root;
// nullptr (and `fill` not called) if `from` is on no screen.
Sheet* openSheet(Widget& from, const std::function<void(Sheet&)>& fill);
// A question before something that cannot be taken back: the title, what it means (gray, up to three lines), the answer
// (`onOk` runs after the sheet closed) and Cancel.
Sheet* confirmSheet(Widget& from, const std::string& title, const std::string& body, const std::string& okText, std::function<void()> onOk);

// One line at the bottom, ink pill with paper text. Created by Root::showToast; touches pass through.
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
  Label* hint() const { return hint_; }  // null when made without a hint

 private:
  Button* button_ = nullptr;
  Label* hint_ = nullptr;
};

}  // namespace pos::ui
