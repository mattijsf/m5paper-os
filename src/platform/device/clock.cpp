#include <Arduino.h>
#include <M5Unified.h>

#include <ctime>

#include "core/timefmt.h"
#include "platform/device/device.h"

namespace pos::device {

uint32_t Clock::millis() { return ::millis(); }

int64_t Clock::now() {
  if (!M5.Rtc.isEnabled()) return 0;
  auto dt = M5.Rtc.getDateTime();
  if (dt.date.year < 2024) return 0;  // the RTC powers up in 2000 or so: treat as never set
  return static_cast<int64_t>(daysFromCivil(dt.date.year, dt.date.month, dt.date.date)) * 86400 + dt.time.hours * 3600 +
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
