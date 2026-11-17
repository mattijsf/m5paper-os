// Time for people: local time from the unix time and the configured time zone (net.timezone, a POSIX TZ string),
// calendar dates as day numbers, and the few text forms every screen uses ("14:05", weekday and month names).
#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace pos {

// A clock at or before this was never set (the RTC starts in 2000; SNTP and the host give a later time).
constexpr int64_t kClockSetAfter = 1700000000;
inline bool clockIsSet(int64_t unixSeconds) { return unixSeconds > kClockSetAfter; }

// Makes `tz` the process time zone (TZ environment variable + tzset).
void applyTimezone(const std::string& tz);
// Broken-down local time. False if the clock is not set (unixSeconds <= 0).
bool localTime(int64_t unixSeconds, struct tm& out);

// ---- calendar dates as day numbers (proleptic Gregorian, 1970-01-01 = 0; Howard Hinnant's civil-date algorithms)

struct Date {
  int year = 1970, month = 1, day = 1;  // month 1..12
  bool operator==(const Date& o) const { return year == o.year && month == o.month && day == o.day; }
};
int daysFromCivil(int year, int month, int day);
inline int daysFromCivil(const Date& d) { return daysFromCivil(d.year, d.month, d.day); }
Date civilFromDays(int days);
int weekdayOf(int days);  // 0 = Sunday
// The local date of a moment as a day number; false if the clock is not set.
bool localDay(int64_t unixSeconds, int& days);

// ---- text

// "2026-11-16".
std::string isoDate(const Date& d);
inline std::string isoDate(int days) { return isoDate(civilFromDays(days)); }

// "09:05" for a minute of the day; 1440 is "24:00" (an event that ends at midnight), later minutes wrap.
std::string hhmm(int minuteOfDay);
// The local time of day, "14:05"; "" if the clock is not set.
std::string clockText(int64_t unixSeconds);
// English names: "Sunday" (0 = Sunday), "January" (0 = January). The short forms are the first three letters.
const char* weekdayName(int weekday);
const char* monthName(int month0);
std::string weekdayShort(int weekday);
std::string monthShort(int month0);

}  // namespace pos
