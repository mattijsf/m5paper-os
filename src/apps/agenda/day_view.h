// The agenda's day view (docs/apps/agenda.md "Day"): one day as time blocks on an hour grid. A header row with the day's name
// and arrows to the previous and next day, a band with the all-day events, then the grid (apps/agenda/day_layout). Today
// gets a line at the current time; events that are over are drawn as outlines, the one that is on now in ink.
// Swipe left / right or the arrows change the day; a tap on a block opens it (onOpen).
#pragma once

#include <functional>
#include <vector>

#include "apps/agenda/day_layout.h"
#include "core/agenda.h"
#include "ui/widget.h"

namespace pos::agenda {

class DayView : public ui::Widget {
 public:
  // `today` / `minute`: the Paper's clock (minute -1: the clock is not set, no "now"). The view reads `agenda` while it lives.
  DayView(const Agenda& agenda, int today, int minute, int first, int last);
  std::function<void(int date)> onDay;                  // the shown day changed
  std::function<void(int date, int event)> onOpen;      // a block or an all-day chip was tapped

  void setDate(int date);  // clamped to [first, last]
  // The clock moved: the now line and which blocks are over / on now. Redraws only when the minute changed (the display
  // manager then pushes just the rows that differ).
  void setNow(int minute);
  int date() const { return date_; }

  ui::Size measure(int availW) override { return {availW, 0}; }
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;

  static constexpr int kHeaderH = 72;
  static constexpr int kChipH = 40;
  static constexpr int kGutter = 64;  // hour labels left of the grid
  static constexpr int kMaxChips = 2;

 private:
  const Day* day() const;
  struct Hit {
    Rect r;
    int event;
  };
  int bandHeight() const;
  Rect gridRect() const;

  const Agenda& agenda_;
  int today_, minute_, first_, last_, date_;
  int pressed_ = 0;  // -1 previous, 1 next (arrow feedback)
  std::vector<Hit> hits_;  // filled by draw()
};

}  // namespace pos::agenda
