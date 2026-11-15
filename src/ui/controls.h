// Touch controls with pressed states (docs/UI.md "Widgets", docs/DISPLAY.md "Rules for UI code"):
// Button, IconButton, Toggle, Badge. Pressed / on / selected look is pure ink and paper, so the
// Fast pass already shows the final state. The action fires on Tap (finger up inside), not on Down.
#pragma once

#include <functional>
#include <string>

#include "ui/label.h"
#include "ui/widget.h"

namespace pos::ui {

// Base for anything tappable: tracks the pressed state and fires `onClick` on Tap.
class Pressable : public Widget {
 public:
  std::function<void()> onClick;
  std::function<void()> onLongPress;
  bool enabled = true;

  bool pressed() const { return pressed_; }
  // Forces the look (tests, specimen sheets); a real touch sets it on Down and clears it on Tap/Cancel.
  void setPressed(bool p);
  bool onTouch(const Gesture& g) override;

 protected:
  virtual void activate() { if (onClick) onClick(); }  // Tap
  bool pressed_ = false;
};

class Button : public Pressable {
 public:
  enum class Kind : uint8_t { Primary, Secondary, Text };
  explicit Button(std::string text = "", Kind kind = Kind::Primary) : kind(kind), text_(std::move(text)) {}
  Kind kind;
  const std::string& text() const { return text_; }
  void setText(const std::string& t) { if (t != text_) { text_ = t; invalidate(Hint::Content); } }
  Size measure(int availW) override;  // 72 high; wide enough for the text (at least 96)
  void draw(Canvas& c) override;

 private:
  std::string text_;
};

// 72 x 72 target with a 32 px icon.
class IconButton : public Pressable {
 public:
  explicit IconButton(Icon icon = Icon::Check) : icon(icon) {}
  Icon icon;
  Size measure(int) override { return {kTouchMin, kTouchMin}; }
  void draw(Canvas& c) override;
};

// On/off pill 88 x 48 in an 88 x 72 target. On = filled ink with a paper knob.
class Toggle : public Pressable {
 public:
  explicit Toggle(bool on = false) : on_(on) {}
  std::function<void(bool)> onChange;
  bool on() const { return on_; }
  void setOn(bool on, Hint h = Hint::Feedback);  // no callback
  Size measure(int) override { return {88, kTouchMin}; }
  void draw(Canvas& c) override;

 protected:
  void activate() override;

 private:
  bool on_;
};

// Filled ink pill with a count ("99+" above 99).
class Badge : public Widget {
 public:
  explicit Badge(int count = 0) : count_(count) {}
  int count() const { return count_; }
  void setCount(int n) { if (n != count_) { count_ = n; invalidate(Hint::Content); } }
  Size measure(int) override;
  void draw(Canvas& c) override;

 private:
  int count_;
};

// Draws a badge pill with its right edge at `right`, vertically centred on `cy`; returns its width.
int drawBadge(Canvas& c, int right, int cy, int count, Color bg);
std::string badgeText(int count);

// Rounded rectangle outline of `stroke` px, drawn inwards.
void strokeRound(Canvas& c, Rect r, int radius, int stroke, Color color);

}  // namespace pos::ui
