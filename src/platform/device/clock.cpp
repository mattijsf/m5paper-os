#include <Arduino.h>
#include <M5Unified.h>

#include <ctime>

#include "platform/device/device.h"

namespace pos::device {

namespace {

// Days since 1970-01-01 for a civil date (proleptic Gregorian).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

}  // namespace

uint32_t Clock::millis() { return ::millis(); }

int64_t Clock::now() {
  if (!M5.Rtc.isEnabled()) return 0;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024) return 0;  // the RTC powers up in 2000 or so: treat as never set
  return daysFromCivil(dt.date.year, dt.date.month, dt.date.date) * 86400 + dt.time.hours * 3600 +
         dt.time.minutes * 60 + dt.time.seconds;
}

void Clock::set(int64_t unixSeconds) {
  if (!M5.Rtc.isEnabled()) return;
  time_t t = static_cast<time_t>(unixSeconds);
  struct tm tmv;
  gmtime_r(&t, &tmv);
  M5.Rtc.setDateTime(&tmv);  // the RTC holds UTC; time zones are applied by core/time
}

}  // namespace pos::device
