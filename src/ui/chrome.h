// Screen chrome (docs/UI.md "Screen anatomy"): StatusBar (56 px), TitleBar (72 px), TabBar (96 px).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/controls.h"

namespace pos::ui {

// Clock on the left; Wi-Fi state and battery on the right; a divider line below. Drawn by the system.
class StatusBar : public Widget {
 public:
  Size measure(int availW) override { return {availW, kStatusBarH}; }
  void draw(Canvas& c) override;
  void setTime(const std::string& t);               // "12:34", "--:--" when the clock is not set
  void setBattery(int percent, bool charging);
  void setWifi(bool connected);

 private:
  std::string time_ = "--:--";
  int percent_ = -1;
  bool charging_ = false, wifi_ = false;
};

// Back arrow (72 x 72 target, long press = launcher), title, optional action button on the right.
class TitleBar : public Widget {
 public:
  TitleBar();
  std::function<void()> onBack, onHome;
  void setTitle(const std::string& t);
  const std::string& title() const { return title_->text(); }
  // A 72 x 72 icon button at the right edge; clearAction() hides it.
  void setAction(Icon icon, std::function<void()> fn);
  void clearAction();
  IconButton* backButton() const { return back_; }

  Size measure(int availW) override { return {availW, kTitleBarH}; }
  void layout() override;

 private:
  IconButton *back_, *action_;
  Label* title_;
};

// Bottom tabs: icon + label, selected = filled ink. 96 px high.
class TabBar : public Widget {
 public:
  struct Tab {
    Icon icon;
    std::string label;
  };
  explicit TabBar(std::vector<Tab> tabs, int selected = 0) : tabs_(std::move(tabs)), selected_(selected) {}
  std::function<void(int)> onSelect;
  int selected() const { return selected_; }
  void setSelected(int i);
  Size measure(int availW) override { return {availW, kTabBarH}; }
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;

 private:
  int indexAt(int x) const;
  std::vector<Tab> tabs_;
  int selected_;
  int pressed_ = -1;
};

}  // namespace pos::ui
