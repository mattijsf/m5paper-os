// Local time from the unix time and the configured time zone (net.timezone, a POSIX TZ string).
#pragma once

#include <cstdint>
#include <ctime>
#include <string>

namespace pos {

// Makes `tz` the process time zone (TZ environment variable + tzset).
void applyTimezone(const std::string& tz);
// Broken-down local time. False if the clock is not set (unixSeconds <= 0).
bool localTime(int64_t unixSeconds, struct tm& out);

}  // namespace pos
