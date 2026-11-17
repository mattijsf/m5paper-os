#include "core/timefmt.h"

#include <cstdio>
#include <cstdlib>

namespace pos {

void applyTimezone(const std::string& tz) {
  setenv("TZ", tz.c_str(), 1);
  tzset();
}

bool localTime(int64_t unixSeconds, struct tm& out) {
  if (unixSeconds <= 0) return false;
  time_t t = static_cast<time_t>(unixSeconds);
  return localtime_r(&t, &out) != nullptr;
}

int daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

Date civilFromDays(int days) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  Date out;
  out.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  out.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  out.year = static_cast<int>(yoe) + era * 400 + (out.month <= 2);
  return out;
}

int weekdayOf(int days) { return ((days % 7) + 11) % 7; }  // 1970-01-01 was a Thursday

bool localDay(int64_t unixSeconds, int& days) {
  struct tm tmv;
  if (!localTime(unixSeconds, tmv)) return false;
  days = daysFromCivil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
  return true;
}

std::string isoDate(const Date& d) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.year, d.month, d.day);
  return buf;
}

std::string hhmm(int minuteOfDay) {
  if (minuteOfDay == 1440) return "24:00";
  char buf[16];
  std::snprintf(buf, sizeof buf, "%02d:%02d", (minuteOfDay / 60) % 24, minuteOfDay % 60);
  return buf;
}

std::string clockText(int64_t unixSeconds) {
  struct tm tmv;
  if (!localTime(unixSeconds, tmv)) return "";
  return hhmm(tmv.tm_hour * 60 + tmv.tm_min);
}

const char* weekdayName(int weekday) {
  static const char* const kNames[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  return kNames[((weekday % 7) + 7) % 7];
}

const char* monthName(int month0) {
  static const char* const kNames[] = {"January", "February", "March",     "April",   "May",      "June",
                                       "July",    "August",   "September", "October", "November", "December"};
  return kNames[((month0 % 12) + 12) % 12];
}

std::string weekdayShort(int weekday) { return std::string(weekdayName(weekday), 3); }
std::string monthShort(int month0) { return std::string(monthName(month0), 3); }

}  // namespace pos
