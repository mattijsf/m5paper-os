// What the daily games share (Somdoku, the word game): today's date from the clock, setting a clock that was never set,
// and copying a score. The text is Dutch like the games.
#pragma once

#include <functional>
#include <string>

#include "core/app.h"
#include "core/timefmt.h"

namespace pos::apps {

// Today's local date; false while the clock was never set (the battery ran flat). `tmOut` gets the broken-down time.
bool today(hal::Clock& clock, Date& date, struct tm* tmOut = nullptr);

// "Klok zetten": the clock from the internet, for a screen that cannot show today's puzzle without it. One per screen
// (a member: the wait ends with the screen).
class ClockSetter {
 public:
  // Joins Wi-Fi and waits for the time; `done` runs when that ended, well or not (ask the clock again). No-op while busy.
  void start(AppContext& ctx, std::function<void()> done);
  bool busy() const { return busy_; }
  // What to say under "the clock is not set": that it is connecting, why the last try failed, else `idle`.
  std::string hint(const std::string& idle) const;

 private:
  bool busy_ = false;
  std::string error_;
};

// Puts a score on the clipboard (source `appId`) and tells the user.
void copyScore(AppContext& ctx, const std::string& score);

// "1 dag", "3 dagen".
std::string daysText(int days);

}  // namespace pos::apps
