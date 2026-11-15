#include "core/demo.h"

namespace pos {

namespace {

struct Tile {
  const char* label;
  const char* glyph;  // placeholder until the icon font exists (Phase 1)
};

const Tile kTiles[] = {
    {"Read", "R"}, {"Home", "H"}, {"WhatsApp", "W"}, {"Mail", "M"}, {"Settings", "S"}, {"Files", "F"},
};

}  // namespace

void drawDemo(Canvas& c, const char* platform) {
  c.fillScreen(kWhite);
  c.setTextColor(kBlack);

  // Status bar
  c.setFont(&fonts::DejaVu18);
  c.setTextDatum(textdatum_t::middle_left);
  c.drawString("12:34", 24, 28);
  c.setTextDatum(textdatum_t::middle_right);
  c.drawString("Wi-Fi  87%", kScreenW - 24, 28);
  c.drawFastHLine(0, 56, kScreenW, gray(11));

  // Title
  c.setFont(&fonts::DejaVu40);
  c.setTextDatum(textdatum_t::top_left);
  c.drawString("PaperOS", 24, 88);
  c.setFont(&fonts::DejaVu18);
  c.setTextColor(gray(6));
  c.drawString("Phase 0 spike", 26, 140);
  c.setTextColor(kBlack);

  // App grid: 2 columns x 3 rows
  const int margin = 24, gap = 24, top = 200;
  const int tileW = (kScreenW - 2 * margin - gap) / 2;
  const int tileH = 196;
  for (int i = 0; i < 6; ++i) {
    int x = margin + (i % 2) * (tileW + gap);
    int y = top + (i / 2) * (tileH + gap);
    c.drawRoundRect(x, y, tileW, tileH, 20, kBlack);
    c.drawRoundRect(x + 1, y + 1, tileW - 2, tileH - 2, 19, kBlack);
    c.fillCircle(x + tileW / 2, y + 78, 40, gray(13));
    c.setFont(&fonts::DejaVu40);
    c.setTextDatum(textdatum_t::middle_center);
    c.drawString(kTiles[i].glyph, x + tileW / 2, y + 80);
    c.setFont(&fonts::DejaVu24);
    c.drawString(kTiles[i].label, x + tileW / 2, y + 158);
  }

  // 16 gray steps: checks quantisation and panel gray rendering
  const int barY = 880, barH = 24, stepW = 30, barX = (kScreenW - 16 * stepW) / 2;
  for (int i = 0; i < 16; ++i) c.fillRect(barX + i * stepW, barY, stepW, barH, gray(i));
  c.drawRect(barX, barY, 16 * stepW, barH, kBlack);

  c.setFont(&fonts::DejaVu12);
  c.setTextDatum(textdatum_t::bottom_center);
  c.setTextColor(gray(5));
  c.drawString(platform, kScreenW / 2, kScreenH - 12);
  c.setTextColor(kBlack);
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
