// The agenda: the paired phone's calendar for the next days (docs/COMPANION.md 7.1 `agenda`, DECISIONS.md D28). One way: the
// phone sends runs of days through the encrypted relay, the Paper keeps them and shows them (apps/agenda, the launcher's
// line, the sleep screen). Pure logic over strings and one file; unit-tested.
//
// File: /data/cache/agenda/agenda.json (the decrypted days; private, deleted with the pairing).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pos::agenda {

constexpr int kMaxRunDays = 14;        // a run (`from`..`to`) covers at most this many days
constexpr int kMaxEventsPerDay = 12;
constexpr size_t kMaxTitleChars = 80, kMaxLocationChars = 60;

struct Event {
  int start = -1;  // minutes after the day's midnight on the phone; -1: began before this day
  int end = -1;    // 1..1440; -1: ends after this day. Both -1: all day
  std::string title, location;
  bool allDay() const { return start < 0 && end < 0; }
  bool operator==(const Event& o) const { return start == o.start && end == o.end && title == o.title && location == o.location; }
};
struct Day {
  int date = 0;  // days since 1970-01-01 (a calendar date, no time zone)
  std::vector<Event> events;
  int more = 0;  // events the phone left out
  bool operator==(const Day& o) const { return date == o.date && more == o.more && events == o.events; }
};
struct Agenda {
  std::vector<Day> days;  // ascending by date, only days with events
  int64_t updated = 0;    // the Paper's clock when the last `agenda` was applied (0: never, or the clock was not set)
  bool received = false;  // the phone has sent an agenda at least once
  // Same days and events (what the views show); `updated` is left out.
  bool sameContent(const Agenda& o) const { return received == o.received && days == o.days; }
};

// Dates are day numbers (core/timefmt.h: daysFromCivil, civilFromDays, weekdayOf).
bool parseDate(const std::string& text, int& days);  // "2026-11-16"; false for anything else, 31 February included
std::string formatDate(int days);

bool load(Agenda& out);  // false when there is no file or it is unreadable (out is then empty)
bool save(const Agenda& a);
void forget();           // deletes the file (unpairing)

// Applies the plaintext of one `agenda` message: every day of its run is replaced, and the days before today go (when
// the clock is set). False (and `a` untouched) if it is not a well-formed `agenda`. `now` is the Paper's clock (unix
// seconds, 0 = not set).
bool apply(Agenda& a, const std::string& plain, int64_t now);
// Drops the days before `today`; true if something went.
bool expire(Agenda& a, int today);

// The Paper's own date (day number) and minute of the day, in its time zone. False if the clock is not set.
bool localNow(int64_t now, int& today, int& minute);

// "All day", "09:30 - 10:15", "From 22:00", "Until 14:00".
std::string timeText(const Event& e);
// "Today", "Tomorrow", else "Wednesday 18 November".
std::string dayTitle(int date, int today);
// One line for the launcher and the sleep screen: what is on now or comes next. "" when nothing is left in the agenda.
//   "Now: Dentist until 10:15"   "14:00 Lunch with Anna"   "Today: Anna's birthday"
//   "Tomorrow 09:30 Dentist"     "Wed 18 Nov: Holiday"
std::string nextLine(const Agenda& a, int today, int minute);
// The same for a clock reading: loads nothing, "" when the clock is not set.
std::string nextLine(const Agenda& a, int64_t now);

}  // namespace pos::agenda
