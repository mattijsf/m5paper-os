#include "apps/agenda/day_view.h"

#include <algorithm>
#include <cstdio>

#include "ui/text.h"
#include "ui/theme.h"

namespace pos::agenda {

namespace {

using ui::Anchor;
using ui::Type;
using gen::Icon;

constexpr Color kBlockFill = gray(12);  // an event still to come: a light block with an ink bar (gray(13) vanishes on the panel)
constexpr int kBar = 6;                 // the ink bar on a block's left edge
constexpr int kMinBlock = 34;           // a short event still gets one line
constexpr int kPad = 10;

std::string hhmm(int minutes) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", (minutes / 60) % 24, minutes % 60);
  return buf;
}

bool inside(const Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

std::string fullDate(int date) {
  // dayTitle() for a day that is neither today nor tomorrow: "Thursday 15 January".
  return dayTitle(date, date + 1000);
}

}  // namespace

DayView::DayView(const Agenda& agenda, int today, int minute, int first, int last)
    : agenda_(agenda), today_(today), minute_(minute), first_(first), last_(std::max(first, last)), date_(first) {
  flex = 1;
}

const Day* DayView::day() const {
  for (const Day& d : agenda_.days)
    if (d.date == date_) return &d;
  return nullptr;
}

void DayView::setDate(int date) {
  date = std::clamp(date, first_, last_);
  pressed_ = 0;
  if (date == date_) {
    invalidateRect(Rect{frame.x, frame.y, frame.w, static_cast<int16_t>(kHeaderH)}, Hint::Feedback);
    return;
  }
  date_ = date;
  // A new day is a new screen's worth of content: Screen lets the display manager show the gray blocks in one Text pass
  // (screen-gray) instead of a Fast pass and a settle sweep over the whole grid 0.6 s later, and lets it put a due clean
  // here, where the user expects change (DISPLAY.md).
  invalidate(Hint::Screen);
  if (onDay) onDay(date_);
}

void DayView::setNow(int minute) {
  if (minute == minute_) return;
  minute_ = minute;
  if (date_ == today_) invalidate(Hint::Content);
}

int DayView::bandHeight() const {
  const Day* d = day();
  if (!d) return 0;
  int chips = 0;
  for (const Event& e : d->events) chips += e.allDay();
  const int shown = std::min(chips, kMaxChips);
  const bool moreLine = chips > kMaxChips || d->more > 0;
  if (shown == 0 && !moreLine) return 0;
  return shown * (kChipH + ui::s1) + (moreLine ? 30 : 0) + ui::s1;
}

Rect DayView::gridRect() const {
  const int top = frame.y + kHeaderH + bandHeight() + 16;  // room for the first hour label
  return Rect{static_cast<int16_t>(frame.x + kGutter), static_cast<int16_t>(top), static_cast<int16_t>(frame.w - kGutter),
              static_cast<int16_t>(std::max(0, frame.y + frame.h - top - 16))};
}

void DayView::draw(Canvas& c) {
  hits_.clear();
  c.fillRect(frame.x, frame.y, frame.w, frame.h, ui::paper);

  // Header: previous, the day, next.
  const int hy = frame.y, cy = hy + kHeaderH / 2;
  auto arrow = [&](int which, Icon icon, int cx) {
    const bool can = which < 0 ? date_ > first_ : date_ < last_;
    const bool down = pressed_ == which && can;
    if (down) c.fillRoundRect(cx - 32, cy - 32, 64, 64, ui::kRadiusSmall, ui::ink);
    ui::drawIcon(c, icon, cx, cy, 32, down ? ui::paper : can ? ui::ink : ui::disabled, down ? ui::ink : ui::paper);
  };
  arrow(-1, Icon::ChevronLeft, frame.x + 32);
  arrow(1, Icon::ChevronRight, frame.x + frame.w - 32);
  const int titleW = frame.w - 2 * 80;
  const bool near = date_ == today_ || date_ == today_ + 1;
  if (near) {
    ui::drawText(c, dayTitle(date_, today_), frame.x + frame.w / 2, hy + 22, Type::Title, ui::ink, Anchor::MiddleCenter, ui::paper);
    ui::drawText(c, ui::ellipsize(c, fullDate(date_), Type::Caption, titleW), frame.x + frame.w / 2, hy + 54, Type::Caption, ui::secondary,
                 Anchor::MiddleCenter, ui::paper);
  } else {
    ui::drawText(c, ui::ellipsize(c, fullDate(date_), Type::Title, titleW), frame.x + frame.w / 2, cy, Type::Title, ui::ink,
                 Anchor::MiddleCenter, ui::paper);
  }

  const Day* d = day();
  static const Day kEmpty;
  const Day& dd = d ? *d : kEmpty;

  // All-day band: outlined chips, then what did not fit.
  int y = hy + kHeaderH;
  int chips = 0;
  for (int i = 0; i < static_cast<int>(dd.events.size()); ++i) {
    const Event& e = dd.events[i];
    if (!e.allDay()) continue;
    if (++chips > kMaxChips) continue;
    const Rect r{static_cast<int16_t>(frame.x), static_cast<int16_t>(y), static_cast<int16_t>(frame.w), static_cast<int16_t>(kChipH)};
    c.drawRoundRect(r.x, r.y, r.w, r.h, ui::kRadiusSmall, ui::ink);
    c.drawRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, ui::kRadiusSmall - 1, ui::ink);
    std::string text = e.title.empty() ? "(No title)" : e.title;
    if (!e.location.empty()) text += "  \xC2\xB7  " + e.location;
    ui::drawText(c, ui::ellipsize(c, text, Type::BodyStrong, r.w - 2 * 16), r.x + 16, r.y + r.h / 2, Type::BodyStrong, ui::ink,
                 Anchor::MiddleLeft, ui::paper);
    hits_.push_back(Hit{r, i});
    y += kChipH + ui::s1;
  }
  const int extra = std::max(0, chips - kMaxChips);
  if (extra > 0 || dd.more > 0) {
    std::string line;
    if (extra > 0) line = "+" + std::to_string(extra) + " more all day";
    if (dd.more > 0) line += (line.empty() ? "" : ", ") + std::to_string(dd.more) + (dd.more == 1 ? " event" : " events") + " only on your phone";
    ui::drawText(c, ui::ellipsize(c, line, Type::Caption, frame.w), frame.x, y + 12, Type::Caption, ui::secondary, Anchor::MiddleLeft, ui::paper);
  }

  // The hour grid.
  const Rect g = gridRect();
  if (g.h < 60) return;
  const Window w = windowFor(dd);
  const int hours = w.span() / 60;
  const int step = g.h / std::max(1, hours) >= 36 ? 1 : g.h / std::max(1, hours) >= 18 ? 2 : 3;
  for (int m = w.from; m <= w.to; m += 60) {
    const int ly = g.y + rowOf(m, w, g.h);
    if (((m - w.from) / 60) % step != 0 && m != w.to) continue;
    c.drawFastHLine(g.x, ly, g.w, ui::divider);
    ui::drawText(c, hhmm(m == 24 * 60 ? 0 : m), g.x - 12, ly, Type::Caption, ui::secondary, Anchor::MiddleRight, ui::paper);
  }

  // Now: an ink line across the grid with a dot on the left (today only; blocks are drawn over it).
  const bool isToday = date_ == today_ && minute_ >= 0;
  if (isToday && minute_ >= w.from && minute_ <= w.to) {
    const int ny = g.y + rowOf(minute_, w, g.h);
    c.fillRect(g.x, ny - 1, g.w, 3, ui::ink);
    c.fillCircle(g.x, ny, 7, ui::ink);
  }

  const std::vector<Block> blocks = layoutBlocks(dd, w, g.h, kMinBlock);
  for (const Block& b : blocks) {
    const Event& e = dd.events[b.event];
    const int colW = (g.w - 6) / b.cols;
    const Rect r{static_cast<int16_t>(g.x + 6 + b.col * colW), static_cast<int16_t>(g.y + b.top + 1),
                 static_cast<int16_t>(colW - 4), static_cast<int16_t>(std::max(kMinBlock - 2, b.bottom - b.top - 2))};
    const int start = e.start < 0 ? 0 : e.start, end = e.end < 0 ? 24 * 60 : e.end;
    const bool past = isToday ? end <= minute_ : date_ < today_;
    const bool now = isToday && start <= minute_ && minute_ < end;
    Color back = kBlockFill, text = ui::ink, sub = ui::ink;
    int left = r.x + kBar + kPad;
    if (now) {
      back = ui::ink;
      text = sub = ui::paper;
      c.fillRoundRect(r.x, r.y, r.w, r.h, ui::kRadiusSmall, back);
      left = r.x + kPad + 4;
    } else if (past) {
      back = ui::paper;
      text = sub = ui::secondary;
      c.fillRoundRect(r.x, r.y, r.w, r.h, ui::kRadiusSmall, back);
      c.drawRoundRect(r.x, r.y, r.w, r.h, ui::kRadiusSmall, ui::divider);
      left = r.x + kPad + 4;
    } else {
      c.fillRoundRect(r.x, r.y, r.w, r.h, ui::kRadiusSmall, back);
      c.fillRoundRect(r.x, r.y, kBar + ui::kRadiusSmall, r.h, ui::kRadiusSmall, ui::ink);
      c.fillRect(r.x + kBar, r.y, ui::kRadiusSmall, r.h, back);  // square inner edge of the bar
    }
    const int textW = r.x + r.w - left - 8;
    const std::string title = e.title.empty() ? "(No title)" : e.title;
    if (textW > 24) {
      const bool twoLines = r.h >= 62;
      // One line: the time goes on the right when the title leaves room for it.
      const std::string when = timeText(e);
      const int whenW = twoLines ? 0 : ui::measureText(c, when, Type::Caption);
      const bool sideTime = !twoLines && textW - whenW - 16 >= 120;
      const int titleW = sideTime ? textW - whenW - 16 : textW;
      ui::drawText(c, ui::ellipsize(c, title, Type::BodyStrong, titleW), left, twoLines ? r.y + 6 : r.y + r.h / 2, Type::BodyStrong, text,
                   twoLines ? Anchor::TopLeft : Anchor::MiddleLeft, back);
      if (sideTime) ui::drawText(c, when, r.x + r.w - 10, r.y + r.h / 2, Type::Caption, sub, Anchor::MiddleRight, back);
      if (twoLines) {
        std::string line = timeText(e);
        if (!e.location.empty()) line += "  \xC2\xB7  " + e.location;
        ui::drawText(c, ui::ellipsize(c, line, Type::Caption, textW), left, r.y + 36, Type::Caption, sub, Anchor::TopLeft, back);
      }
    }
    hits_.push_back(Hit{r, b.event});
  }

  if (dd.events.empty()) {
    const int my = g.y + rowOf(w.from + (w.span() / 120) * 60 + 30, w, g.h);  // between two hour lines near the middle
    ui::drawText(c, date_ < first_ ? "" : "Nothing planned", g.x + g.w / 2, my, Type::Body, ui::secondary, Anchor::MiddleCenter, ui::paper);
  }
}

bool DayView::onTouch(const Gesture& g) {
  const bool inHeader = g.startY >= frame.y && g.startY < frame.y + kHeaderH;
  const int which = !inHeader ? 0 : g.startX < frame.x + 80 ? -1 : g.startX >= frame.x + frame.w - 80 ? 1 : 0;
  switch (g.type) {
    case Gesture::Down:
      if (which != 0 && (which < 0 ? date_ > first_ : date_ < last_)) {
        pressed_ = which;
        invalidateRect(Rect{frame.x, frame.y, frame.w, static_cast<int16_t>(kHeaderH)}, Hint::Feedback);
      }
      return true;
    case Gesture::Tap:
      if (which != 0) {
        setDate(date_ + which);
        return true;
      }
      for (const Hit& h : hits_) {
        if (!inside(h.r, g.x, g.y)) continue;
        if (onOpen) onOpen(date_, h.event);
        return true;
      }
      return true;
    case Gesture::SwipeLeft:
      setDate(date_ + 1);
      return true;
    case Gesture::SwipeRight:
      setDate(date_ - 1);
      return true;
    case Gesture::Cancel:
    case Gesture::LongPress:
      if (pressed_ != 0) {
        pressed_ = 0;
        invalidateRect(Rect{frame.x, frame.y, frame.w, static_cast<int16_t>(kHeaderH)}, Hint::Feedback);
      }
      return true;
    default:
      return false;
  }
}

}  // namespace pos::agenda
