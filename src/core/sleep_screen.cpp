#include "core/sleep_screen.h"

#include <algorithm>
#include <cstdlib>

#include "core/fs.h"
#include "core/store.h"

#include "ui/text.h"
#include "ui/theme.h"

namespace pos {

std::string sleepNote(int unread, const std::string& updated) {
  std::string s;
  if (unread > 0) s = std::to_string(unread) + " unread";
  if (!updated.empty()) s += (s.empty() ? "Updated " : " - updated ") + updated;
  return s;
}

int cachedUnread() {
  int n = 0;
  for (const AppEntry* a : AppRegistry::instance().sorted()) {
    std::string raw;
    fs::readFile(std::string("/data/cache/") + a->info.id + "/badge", raw);
    n += std::max(0, std::atoi(raw.c_str()));
  }
  return n;
}

void saveSleepInfo(const SleepInfo* info) {
  Store sys("/data/system.json");
  sys.set("sleep.has", info != nullptr);
  sys.set("sleep.title", info ? info->title : std::string());
  sys.set("sleep.subtitle", info ? info->subtitle : std::string());
  sys.set("sleep.percent", info ? info->percent : -1);
  sys.set("sleep.image", info ? info->image : std::string());
  sys.save();
}

bool loadSleepInfo(SleepInfo& info) {
  Store sys("/data/system.json");
  if (!sys.get("sleep.has", false)) return false;
  info.title = sys.get("sleep.title", std::string());
  info.subtitle = sys.get("sleep.subtitle", std::string());
  info.percent = sys.get("sleep.percent", -1);
  info.image = sys.get("sleep.image", std::string());
  return !info.title.empty();
}

void drawSleepScreen(Canvas& c, const SleepInfo* info, Canvas* image, const std::string& note) {
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
  if (!note.empty()) ui::drawText(c, ui::ellipsize(c, note, Type::Body, 492), cx, 800, Type::Body, ui::ink, Anchor::MiddleCenter, kWhite);  // content: ink
  ui::drawText(c, "Press the side button to wake", cx, 860, Type::Body, ui::secondary, Anchor::MiddleCenter, kWhite);
}

}  // namespace pos
