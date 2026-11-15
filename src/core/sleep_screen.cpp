#include "core/sleep_screen.h"

#include <algorithm>

#include "ui/text.h"
#include "ui/theme.h"

namespace pos {

void drawSleepScreen(Canvas& c, const SleepInfo* info, Canvas* image) {
  using ui::Anchor;
  using ui::Type;
  c.fillRect(0, 0, kScreenW, kScreenH, kWhite);
  const int cx = kScreenW / 2;
  if (info) {
    const int top = 110;
    if (image) {
      image->pushSprite(&c, cx - kSleepImageW / 2, top);
      c.drawRect(cx - kSleepImageW / 2, top, kSleepImageW, kSleepImageH, ui::ink);
    } else {
      c.drawRect(cx - 60, top + 90, 120, 180, ui::ink);
      ui::drawIcon(c, Icon::BookOpen, cx, top + 180, 64, ui::secondary, kWhite);
    }
    int y = top + kSleepImageH + 56;
    ui::drawText(c, ui::ellipsize(c, info->title, Type::Title, 480), cx, y, Type::Title, ui::ink, Anchor::MiddleCenter, kWhite);
    y += 40;
    if (!info->subtitle.empty()) {
      ui::drawText(c, ui::ellipsize(c, info->subtitle, Type::Body, 480), cx, y, Type::Body, ui::ink, Anchor::MiddleCenter, kWhite);
      y += 44;
    }
    if (info->percent >= 0) {
      const int w = 300, x = cx - w / 2;
      c.fillRect(x, y, w, 8, gray(12));
      c.fillRect(x, y, w * std::min(100, info->percent) / 100, 8, ui::ink);
      ui::drawText(c, std::to_string(info->percent) + "% read", cx, y + 32, Type::Caption, ui::secondary, Anchor::MiddleCenter, kWhite);
    }
  } else {
    ui::drawIcon(c, Icon::BookOpen, cx, 360, 64, ui::ink, kWhite);
    ui::drawText(c, "PaperOS", cx, 450, Type::Headline, ui::ink, Anchor::MiddleCenter, kWhite);
  }
  ui::drawText(c, "Press the side button to wake", cx, 860, Type::Body, ui::secondary, Anchor::MiddleCenter, kWhite);
}

}  // namespace pos
