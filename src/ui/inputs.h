// Value inputs for e-ink (docs/UI.md "Widgets"): Segmented, Stepper, LevelBar. No sliders: dragging
// is poor on e-ink, so a level is set by tapping one of 10 segments. Pressed / selected = ink and paper.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/controls.h"

namespace pos::ui {

// 2 to 4 options in one outlined bar, 72 high. Selected = filled ink. A finger that stays down (kPressDelayMs) shows the
// option under it as selected; Tap commits it. A quick tap goes straight to the new selection, in one pass with what the
// choice changes on the screen.
class Segmented : public Widget {
 public:
  explicit Segmented(std::vector<std::string> options, int selected = 0)
      : options_(std::move(options)), selected_(selected) {}
  ~Segmented() override { *alive_ = false; }
  std::function<void(int)> onChange;
  int selected() const { return selected_; }
  void setSelected(int i, Hint h = Hint::Feedback);  // no callback
  Size measure(int availW) override { return {availW, kTouchMin}; }
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;

 private:
  int indexAt(int x) const;
  std::vector<std::string> options_;
  int selected_;
  int preview_ = -1;  // option under the finger, shown once it stays down (kPressDelayMs)
  uint32_t pressSeq_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// [-]  value  [+]: two 72 px IconButtons around the value text. The buttons grey out at the limits.
class Stepper : public Widget {
 public:
  Stepper(int value, int min, int max, int step = 1, std::function<std::string(int)> format = {});
  std::function<void(int)> onChange;
  int value() const { return value_; }
  void setValue(int v, Hint h = Hint::Feedback);  // clamped; no callback
  void setRange(int min, int max, Hint h = Hint::Feedback);  // the value is clamped into it; no callback
  Size measure(int availW) override { return {availW, kTouchMin}; }
  void layout() override;
  void draw(Canvas& c) override;

 private:
  void change(int delta);
  int value_, min_, max_, step_;
  std::function<std::string(int)> format_;
  IconButton *minus_, *plus_;
};

// 10 segments, 10..100 %. Tap segment k to set (k+1) * 10.
class LevelBar : public Widget {
 public:
  explicit LevelBar(int percent = 50) : percent_(percent) {}
  std::function<void(int)> onChange;
  int percent() const { return percent_; }
  void setPercent(int p, Hint h = Hint::Feedback);  // rounded to 10, 10..100; no callback
  Size measure(int availW) override { return {availW, kTouchMin}; }
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;

 private:
  int segmentAt(int x) const;
  int percent_;
  int preview_ = 0;  // percent under the finger, 0 = none
};

}  // namespace pos::ui
