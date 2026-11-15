// Label (text, wrapping, ellipsis) and IconView (docs/UI.md "Widgets").
#pragma once

#include <string>
#include <vector>

#include "ui/text.h"
#include "ui/widget.h"

namespace pos::ui {

enum class TextAlign : uint8_t { Left, Center, Right };

class Label : public Widget {
 public:
  explicit Label(std::string text = "", Type type = Type::Body, Color color = ink)
      : text_(std::move(text)), type(type), color(color) {}

  Type type;
  Color color;
  TextAlign align = TextAlign::Left;
  int maxLines = 1;  // text that does not fit ends with an ellipsis
  int lineGap = 4;   // extra px between lines

  const std::string& text() const { return text_; }
  // Changes the text and redraws the label; the size is not recomputed (call Screen::relayout() if it changes).
  void setText(const std::string& t, Hint h = Hint::Content);

  Size measure(int availW) override;
  void draw(Canvas& c) override;

  // The lines the text breaks into for `width` (word wrap, hard break for long words, ellipsis on the last line).
  std::vector<std::string> lines(Canvas& c, int width) const;

 private:
  std::string text_;
};

class IconView : public Widget {
 public:
  IconView(Icon icon, int size = 32, Color color = ink) : icon(icon), size(size), color(color) {}
  Icon icon;
  int size;  // 32 or 64
  Color color;
  Size measure(int) override { return {size, size}; }
  void draw(Canvas& c) override;
};

}  // namespace pos::ui
