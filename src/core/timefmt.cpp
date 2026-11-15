#include "core/timefmt.h"

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

}  // namespace pos
