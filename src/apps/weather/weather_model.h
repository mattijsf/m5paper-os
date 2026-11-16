// Weather model (docs/apps/weather.md): what the app reads from a Home Assistant weather entity (the Google Weather
// integration, weather.thuis) and its forecasts (weather.get_forecasts). Parsing, the saved copy and text helpers;
// platform-free and unit tested (tests/unit/weather_test.cpp).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gen/icons.h"

namespace pos::weather {

constexpr double kNone = -1000;  // a value the answer did not carry

// Current conditions: GET /api/states/<entity>.
struct Current {
  std::string condition;  // Home Assistant condition: sunny, clear-night, partlycloudy, cloudy, rainy, pouring, ...
  double temperature = kNone, apparent = kNone, humidity = kNone, windSpeed = kNone, windGust = kNone, windBearing = kNone;
  std::string tempUnit = "\xC2\xB0" "C", windUnit = "km/h", precipUnit = "mm";
};

// One forecast step (an hour or a day).
struct Step {
  int64_t time = 0;  // unix seconds (UTC) of `datetime`
  std::string condition;
  double temperature = kNone, templow = kNone, precipitation = kNone, precipProbability = kNone;
  int daytime = -1;  // is_daytime: 1, 0, -1 = not given
};

struct Weather {
  Current now;
  std::vector<Step> hourly, daily;
  int64_t fetched = 0;  // unix seconds of the last successful read (0: never)
  bool valid() const { return !now.condition.empty(); }
};

// The entity's state JSON. False if it is not a weather entity's answer (no state).
bool parseState(const std::string& json, Current& out);
// The answer of POST /api/services/weather/get_forecasts?return_response for `entity`: {"service_response":{entity:{"forecast":[...]}}}.
// False if the entity or its forecast list is missing.
bool parseForecast(const std::string& json, const std::string& entity, std::vector<Step>& out);
// "2026-01-15T12:00:00Z", "...+00:00", "...+01:00", fractions ignored -> unix seconds. False if it is not such a time.
bool parseIsoTime(const std::string& s, int64_t& out);

// The saved copy (/data/cache/weather/weather.json).
std::string toJson(const Weather& w);
bool fromJson(const std::string& json, Weather& out);

// Lucide icon and English text for a condition; `night` picks the moon for clear and partly cloudy.
gen::Icon conditionIcon(const std::string& condition, bool night = false);
std::string conditionText(const std::string& condition);
// "SW" for 225 degrees (the direction the wind comes from), "" for none.
std::string compass(double bearing);
// "7°" (rounded; "-0" is "0"), "" for none.
std::string tempText(double v);
// "0.4 mm" with one decimal below 10, "12 mm" above.
std::string amount(double v, const std::string& unit);

// The steps worth showing at `now`: hours from the one that holds `now` on (`count`, `stride` apart); days from today
// (the local date of `now`) on. With no clock (now <= 0) the list starts at its first step.
std::vector<Step> nextHours(const std::vector<Step>& hourly, int64_t now, int count, int stride);
std::vector<Step> nextDays(const std::vector<Step>& daily, int64_t now, int count);
// Local date as a day number (days since 1970-01-01) of a unix time, using the process time zone.
int localDay(int64_t t);

}  // namespace pos::weather
