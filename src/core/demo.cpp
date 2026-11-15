#include "core/demo.h"

#include "ui/text.h"
#include "ui/theme.h"

namespace pos {

void drawFontSheet(Canvas& c) {
  using ui::Type;
  c.fillScreen(ui::paper);
  struct Row {
    Type type;
    const char* name;
  };
  const Row rows[] = {{Type::Caption, "caption 18"}, {Type::Body, "body 22"}, {Type::BodyStrong, "bodyStrong 22"},
                      {Type::Title, "title 28"}, {Type::Headline, "headline 40"}, {Type::ReadM, "read-M 28 (S 24, L 32 not shown)"},
                      {Type::ReadXL, "read-XL 38"},
                      {Type::ReadItalicM, "read-M italic"}, {Type::ReadBoldM, "read-M bold"}};
  int y = 8;
  for (const Row& r : rows) {
    ui::drawText(c, r.name, 12, y, Type::Caption, ui::secondary);
    y += ui::lineHeight(c, Type::Caption);
    std::string line = r.type == Type::Headline ? "Aa 0123 \xE2\x82\xAC" : "Hamburgefonstiv 0123 \xC3\xA0\xC3\xA9\xC3\xAE\xC3\xB5\xC3\xBC \xC5\x81 \xE2\x80\x94 \xE2\x80\x9Cq\xE2\x80\x9D \xE2\x80\x98x\xE2\x80\x99 \xE2\x80\xA6 \xE2\x82\xAC \xE6\x97\xA5";
    ui::drawText(c, ui::ellipsize(c, line, r.type, kScreenW - 24), 12, y, r.type, ui::ink);
    y += ui::lineHeight(c, r.type) + 6;
  }
  // Icons: all of them at 32 px, a few at 64 px, on paper and on a filled well.
  const int n = static_cast<int>(ui::Icon::Count);
  for (int i = 0; i < n; ++i) ui::drawIcon(c, static_cast<ui::Icon>(i), 36 + (i % 9) * 58, y + 24 + (i / 9) * 48, 32, ui::ink);
  y += (n + 8) / 9 * 48 + 8;
  c.fillRoundRect(12, y, 516, 84, 12, ui::fill);
  for (int i = 0; i < 6; ++i) ui::drawIcon(c, static_cast<ui::Icon>(i * 3), 56 + i * 86, y + 42, 64, ui::ink, ui::fill);
}

Rect drawTouchMarker(Canvas& c, int x, int y) {
  const int r = 14;
  c.fillCircle(x, y, r + 3, gray(8));  // soft ring: grays need a settle pass after the Fast push
  c.fillCircle(x, y, r, kBlack);
  const int e = r + 3;  // outer ring
  return Rect{static_cast<int16_t>(x - e), static_cast<int16_t>(y - e), static_cast<int16_t>(2 * e + 1),
              static_cast<int16_t>(2 * e + 1)};
}

}  // namespace pos
