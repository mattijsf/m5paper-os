#include "core/sleep_screen.h"

#include <algorithm>
#include <cstdlib>

#include "core/agenda.h"
#include "core/fs.h"
#include "core/store.h"
#include "core/timefmt.h"

#include "ui/image.h"
#include "ui/text.h"
#include "ui/theme.h"

namespace pos {

namespace {

SleepWeatherSource gWeatherSource = nullptr;

// "Friday 16 January"
std::string dayName(int date) {
  const Date d = civilFromDays(date);
  return std::string(weekdayName(weekdayOf(date))) + " " + std::to_string(d.day) + " " + monthName(d.month - 1);
}

// The state, in words: the panel keeps its picture without power, so nothing else tells off from on.
void drawPoweredOff(Canvas& c) {
  using ui::Anchor;
  using ui::Type;
  const int cx = kScreenW / 2;
  const std::string state = "Powered off";
  const int iconW = 32, gap = 12, textW = ui::measureText(c, state, Type::Title);
  const int left = cx - (iconW + gap + textW) / 2;
  ui::drawIcon(c, Icon::Power, left + iconW / 2, 852, iconW, ui::ink, kWhite);
  ui::drawText(c, state, left + iconW + gap, 852, Type::Title, ui::ink, Anchor::MiddleLeft, kWhite);
  ui::drawText(c, "Press the side button to start", cx, 898, Type::Body, ui::secondary, Anchor::MiddleCenter, kWhite);
}

}  // namespace

void setSleepWeatherSource(SleepWeatherSource source) { gWeatherSource = source; }

std::vector<SleepBoard::Row> sleepAgendaRows(const agenda::Agenda& a, int today, int minute, int maxRows) {
  std::vector<SleepBoard::Row> rows;
  for (const agenda::Day& day : a.days) {
    if (day.date < today) continue;
    std::vector<SleepBoard::Row> events;
    for (const agenda::Event& e : day.events) {
      // Today: what is over is left out (an event that began before today and one without an end are not over).
      if (day.date == today && !e.allDay() && e.end >= 0 && e.end <= minute) continue;
      events.push_back({false, agenda::timeText(e), e.title});
    }
    if (events.empty()) continue;
    const int heading = day.date == today ? 0 : 1;  // the board's date is today's heading
    if (static_cast<int>(rows.size()) + heading + 1 > maxRows) break;
    if (heading) rows.push_back({true, "", dayName(day.date)});
    for (const SleepBoard::Row& r : events) {
      if (static_cast<int>(rows.size()) >= maxRows) break;
      rows.push_back(r);
    }
    if (static_cast<int>(rows.size()) >= maxRows) break;
  }
  return rows;
}

SleepBoard buildSleepBoard(int64_t now) {
  SleepBoard b;
  int today = 0, minute = 0;
  if (!agenda::localNow(now, today, minute)) return b;  // no clock: no date, no "today"
  b.date = dayName(today);
  agenda::Agenda a;
  if (agenda::load(a)) b.agenda = sleepAgendaRows(a, today, minute);
  if (gWeatherSource) gWeatherSource(b, now);
  return b;
}

void drawSleepBoard(Canvas& c, const SleepBoard& b, const SleepInfo* info, Canvas* thumb, const std::string& updated) {
  using ui::Anchor;
  using ui::Type;
  c.fillRect(0, 0, kScreenW, kScreenH, kWhite);
  const int x0 = ui::kMargin, x1 = kScreenW - ui::kMargin;
  ui::drawText(c, b.date, x0, 64, ui::measureText(c, b.date, Type::Headline) <= ui::kContentW ? Type::Headline : Type::Title, ui::ink,
               Anchor::MiddleLeft, kWhite);
  int y = 104;  // top of the next section
  const auto rule = [&] {
    c.fillRect(x0, y + 10, ui::kContentW, 1, ui::divider);
    y += 28;
  };
  if (b.weather) {
    ui::drawIcon(c, b.weatherIcon, x0 + 36, y + 44, 64, ui::ink, kWhite);
    ui::drawText(c, b.temps, x0 + 92, y + 28, Type::Headline, ui::ink, Anchor::MiddleLeft, kWhite);
    ui::drawText(c, ui::ellipsize(c, b.summary, Type::Body, x1 - (x0 + 92)), x0 + 92, y + 68, Type::Body, ui::ink, Anchor::MiddleLeft, kWhite);
    y += 100;
    if (!b.blocks.empty()) {
      const int colW = ui::kContentW / kSleepBlocks;
      for (size_t i = 0; i < b.blocks.size() && i < static_cast<size_t>(kSleepBlocks); ++i) {
        const SleepBoard::Block& k = b.blocks[i];
        const int cx = x0 + colW * static_cast<int>(i) + colW / 2;
        ui::drawText(c, k.label, cx, y + 12, Type::Caption, ui::secondary, Anchor::MiddleCenter, kWhite);
        ui::drawIcon(c, k.icon, cx, y + 46, 36, ui::ink, kWhite);
        ui::drawText(c, k.temp, cx, y + 84, Type::BodyStrong, ui::ink, Anchor::MiddleCenter, kWhite);
        if (!k.rain.empty()) ui::drawText(c, k.rain, cx, y + 110, Type::Caption, ui::ink, Anchor::MiddleCenter, kWhite);
      }
      y += 124;
    }
    rule();
  }
  if (!b.agenda.empty()) {
    const int rowH = 38, timeW = 164;
    for (const SleepBoard::Row& r : b.agenda) {
      if (r.heading) {
        ui::drawText(c, ui::ellipsize(c, r.title, Type::BodyStrong, ui::kContentW), x0, y + rowH / 2, Type::BodyStrong, ui::ink, Anchor::MiddleLeft, kWhite);
      } else {
        ui::drawText(c, r.time, x0, y + rowH / 2, Type::Body, ui::ink, Anchor::MiddleLeft, kWhite);
        ui::drawText(c, ui::ellipsize(c, r.title, Type::Body, ui::kContentW - timeW), x0 + timeW, y + rowH / 2, Type::Body, ui::ink, Anchor::MiddleLeft, kWhite);
      }
      y += rowH;
    }
    rule();
  }
  if (info) {  // the open book, one row
    int tx = x0;
    if (thumb) {
      thumb->pushSprite(&c, x0, y);
      c.drawRect(x0, y, kSleepThumbW, kSleepThumbH, ui::ink);
      tx = x0 + kSleepThumbW + 18;
    }
    ui::drawText(c, ui::ellipsize(c, info->title, Type::BodyStrong, x1 - tx), tx, y + 24, Type::BodyStrong, ui::ink, Anchor::MiddleLeft, kWhite);
    std::string sub = info->subtitle;
    if (info->percent >= 0) sub += (sub.empty() ? "" : ", ") + std::to_string(std::min(100, info->percent)) + "% read";
    ui::drawText(c, ui::ellipsize(c, sub, Type::Body, x1 - tx), tx, y + 58, Type::Body, ui::ink, Anchor::MiddleLeft, kWhite);
  }
  // Unread per app: counts only, never a sender or text.
  int x = x0;
  for (const AppEntry* a : AppRegistry::instance().sorted()) {
    const int n = appBadge(a->info.id);
    if (n <= 0) continue;
    const std::string count = std::to_string(n);
    ui::drawIcon(c, a->info.icon, x + 14, 800, 28, ui::ink, kWhite);
    ui::drawText(c, count, x + 38, 800, Type::Body, ui::ink, Anchor::MiddleLeft, kWhite);
    x += 38 + ui::measureText(c, count, Type::Body) + 28;
  }
  if (!updated.empty()) ui::drawText(c, "Updated " + updated, x1, 800, Type::Body, ui::secondary, Anchor::MiddleRight, kWhite);
  drawPoweredOff(c);
}

void drawSleepScreenFor(Canvas& c, const SleepInfo* info, int64_t now, bool board, const std::string& updated) {
  const bool withImage = info && !info->image.empty();
  if (board) {
    const SleepBoard b = buildSleepBoard(now);
    if (!b.empty()) {
      std::shared_ptr<Canvas> thumb;
      if (withImage) thumb = ui::loadImageFit(info->image, kSleepThumbW, kSleepThumbH);
      drawSleepBoard(c, b, info, thumb.get(), updated);
      return;
    }
  }
  std::shared_ptr<Canvas> image;
  if (withImage) image = ui::loadImageFit(info->image, kSleepImageW, kSleepImageH);
  drawSleepScreen(c, info, image.get(), sleepNote(cachedUnread(), updated), sleepAgendaLine(now));
}

std::string sleepNote(int unread, const std::string& updated) {
  std::string s;
  if (unread > 0) s = std::to_string(unread) + " unread";
  if (!updated.empty()) s += (s.empty() ? "Updated " : " - updated ") + updated;
  return s;
}

int cachedUnread() {
  int n = 0;
  for (const AppEntry* a : AppRegistry::instance().sorted()) n += appBadge(a->info.id);
  return n;
}

std::string sleepAgendaLine(int64_t now) {
  agenda::Agenda a;
  if (!agenda::load(a)) return "";
  return agenda::nextLine(a, now);
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

void drawSleepScreen(Canvas& c, const SleepInfo* info, Canvas* image, const std::string& note, const std::string& agenda) {
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
    ui::drawIcon(c, Icon::Paperos, cx, 360, 64, ui::ink, kWhite);  // the PaperOS mark (assets/icons-drawn/paperos.txt)
    ui::drawText(c, "PaperOS", cx, 450, Type::Headline, ui::ink, Anchor::MiddleCenter, kWhite);
  }
  if (!agenda.empty()) {  // content: ink. With no note below it, it takes the note's place
    ui::drawText(c, ui::ellipsize(c, agenda, Type::Body, 492), cx, note.empty() ? 800 : 756, Type::Body, ui::ink, Anchor::MiddleCenter, kWhite);
  }
  if (!note.empty()) ui::drawText(c, ui::ellipsize(c, note, Type::Body, 492), cx, 800, Type::Body, ui::ink, Anchor::MiddleCenter, kWhite);  // content: ink
  drawPoweredOff(c);
}

}  // namespace pos
