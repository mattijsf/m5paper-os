// Tile: launcher / Home app square with a 64 px icon well, a label, an optional value and badge.
// Pressed = 4 px border (pure ink and paper). docs/UI.md "Widgets".
#pragma once

#include <string>

#include "ui/controls.h"

namespace pos::ui {

class Tile : public Pressable {
 public:
  explicit Tile(Icon icon = Icon::House, std::string label = "") : icon_(icon), label_(std::move(label)) {}
  void setLabel(const std::string& t);
  void setValue(const std::string& t);  // e.g. "21.5 °C": a second line below the label
  void setBadge(int count);             // 0 hides it
  void setIcon(Icon icon);
  const std::string& label() const { return label_; }

  Size measure(int availW) override { return {availW, 196}; }
  void draw(Canvas& c) override;

 private:
  Icon icon_;
  std::string label_, value_;
  int badge_ = 0;
};

}  // namespace pos::ui
