// Tile: an outlined square with a 64 px icon, a label, an optional value and badge. Ink and paper only: a gray well
// behind the icon made every screen of tiles a Text pass (a flicker in three steps, DISPLAY.md "Gray areas cost a
// flicker"). Two sizes: Large (two to a row, 196 high: the Home app's rooms and scenes) and Small (three to a row, 160
// high, the label one size down: the launcher, twelve apps to a page). Pressed = 4 px border (pure ink and paper). docs/UI.md "Widgets".
#pragma once

#include <string>

#include "ui/controls.h"

namespace pos::ui {

class Tile : public Pressable {
 public:
  enum class Size : uint8_t { Large, Small };
  explicit Tile(Icon icon = Icon::House, std::string label = "", Size size = Size::Large)
      : icon_(icon), label_(std::move(label)), size_(size) {}
  void setLabel(const std::string& t);
  void setValue(const std::string& t);  // e.g. "21.5 °C": a second line below the label
  void setBadge(int count);             // 0 hides it
  void setIcon(Icon icon);
  // A blank tile keeps its grid cell but draws nothing and takes no touches (the last row of a grid).
  void setBlank(bool blank);
  const std::string& label() const { return label_; }

  static constexpr int kLargeH = 196, kSmallH = 160;
  ui::Size measure(int availW) override { return {availW, size_ == Size::Small ? kSmallH : kLargeH}; }
  void draw(Canvas& c) override;

 private:
  Icon icon_;
  std::string label_, value_;
  int badge_ = 0;
  bool blank_ = false;
  Size size_;
};

}  // namespace pos::ui
