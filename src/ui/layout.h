// Layout containers (docs/UI.md): Column, Row, Stack, Spacer, Padding. They draw nothing.
// Children with flex > 0 share the space left over along the main axis in proportion to `flex`.
#pragma once

#include "ui/widget.h"

namespace pos::ui {

enum class Align : uint8_t { Start, Center, End, Stretch };

// Children stacked top to bottom, `gap` px apart. Cross axis (width): Stretch by default.
class Column : public Widget {
 public:
  explicit Column(int gap = 0, Align align = Align::Stretch) : gap(gap), align(align) {}
  int gap;
  Align align;
  Size measure(int availW) override;
  void layout() override;
};

// Children left to right, `gap` px apart. Cross axis (height): Center by default.
class Row : public Widget {
 public:
  explicit Row(int gap = 0, Align align = Align::Center) : gap(gap), align(align) {}
  int gap;
  Align align;
  Size measure(int availW) override;
  void layout() override;
};

// Children on top of each other, all given the full frame (later ones are drawn above).
class Stack : public Widget {
 public:
  Size measure(int availW) override;
  void layout() override;
};

// Empty space: fixed `size` along the parent's main axis, or flex.
class Spacer : public Widget {
 public:
  explicit Spacer(int size = 0, int flexWeight = 0) : size(size) { flex = flexWeight; }
  int size;
  Size measure(int) override { return {size, size}; }
};

// One child inset by l, t, r, b.
class Padding : public Widget {
 public:
  Padding(int l, int t, int r, int b) : l(l), t(t), r(r), b(b) {}
  explicit Padding(int all) : l(all), t(all), r(all), b(all) {}
  int l, t, r, b;
  Size measure(int availW) override;
  void layout() override;
};

}  // namespace pos::ui
