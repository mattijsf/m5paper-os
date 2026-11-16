// Weather model (docs/apps/weather.md): what the app reads from a Home Assistant weather entity (the Google Weather
// integration, weather.thuis) and its forecasts (weather.get_forecasts). Parsing, the saved copy and text helpers;
// platform-free and unit tested (tests/unit/weather_test.cpp).
#pragma once

#include <cstdint>
#include <functional>
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

// A few hours of one day taken together (the Hours page): clock-aligned, 00-03, 03-06, ... local time.
struct Block {
  int64_t time = 0;         // unix seconds of its first forecast hour
  int fromHour = 0, toHour = 0;  // local hours it covers: 13 and 15 for the hours 13:00 and 14:00
  int slot = 0;             // which block of the day: local start hour / span (0..7 for span 3)
  std::string condition;    // the condition most of its hours have (the earliest of equals)
  double temperature = kNone;        // mean of its hours
  double precipitation = kNone;      // sum
  double precipProbability = kNone;  // highest
  bool night = false;       // most of its hours are night
};
struct DayBlocks {
  int64_t time = 0;  // unix seconds of the day's first block
  std::vector<Block> blocks;
};

// One block of the minute forecast (Google's nowcast through a Home Assistant REST sensor, sensor.google_neerslag_minuten).
struct RainBlock {
  int64_t start = 0, end = 0;  // unix seconds
  double rate = 0;             // mm per hour over the block (its amount scaled to an hour)
  double probability = 0;      // 0..100
};
constexpr double kWetRate = 0.05;          // mm/h from which a block counts as rain
constexpr int64_t kRainWindowSec = 2 * 3600;  // what the graph shows
constexpr int64_t kRainStaleSec = 40 * 60;    // older than this it is not a forecast for now (the sensor reads every 10 minutes)

struct Weather {
  Current now;
  std::vector<Step> hourly, daily;
  std::vector<RainBlock> rain;      // the minute forecast, if the place has one (blocks that were not over at the read)
  int64_t sunrise = 0, sunset = 0;  // sun.sun's next_rising / next_setting (unix seconds; 0: not known)
  int64_t fetched = 0;  // unix seconds of the last successful read (0: never)
  bool valid() const { return !now.condition.empty(); }
};

// A place the app shows: a weather entity of Home Assistant's Google Weather integration (one per location there).
struct Place {
  std::string name;    // on the switcher: "Thuis", "Office"
  std::string entity;  // "weather.thuis"
  std::string rain;    // the place's minute forecast sensor, "" for none
  bool sun = false;    // sun.sun is this place's sun (Home Assistant's own location)
};
constexpr int kMaxPlaces = 4;
// The places from the app's settings (/data/apps/weather.json; `lookup(key, default)` reads one): keys `entity`, `name`
// for the first, `entity2`..`entity4`, `name2`..`name4` for the others, `rain`, `rain2`..`rain4` for a place's minute
// forecast sensor, `sun` = the number (1..4) of the place sun.sun belongs to (0: none). Defaults: Thuis (weather.thuis,
// with the sun and sensor.google_neerslag_minuten) and Office (weather.office). An empty entity
// leaves a place out; a place without a name gets its entity's ("weather.work_site" -> "Work site").
std::vector<Place> placesFrom(const std::function<std::string(const std::string& key, const std::string& def)>& lookup);
// The saved copy of one place: "/data/cache/weather/weather.thuis.json".
std::string cacheFile(const std::string& entity);

// The entity's state JSON. False if it is not a weather entity's answer (no state).
bool parseState(const std::string& json, Current& out);
// The answer of POST /api/services/weather/get_forecasts?return_response for `entity`: {"service_response":{entity:{"forecast":[...]}}}.
// False if the entity or its forecast list is missing.
bool parseForecast(const std::string& json, const std::string& entity, std::vector<Step>& out);
// The minute forecast sensor's state JSON: attributes.segments[] with timeFrame.startTime/endTime and qpf.quantity (mm in
// the segment). `reported` = when Home Assistant last wrote the sensor (last_reported, else last_updated; 0: not given).
// False if there is no segment list (the sensor is unavailable).
bool parseRain(const std::string& json, std::vector<RainBlock>& out, int64_t& reported);
// What the graph says in one line and how high it goes, for the window [now, now + kRainWindowSec).
struct RainSummary {
  bool known = false;    // a block holds `now`: there is a forecast to show
  bool wet = false;      // rain somewhere in the window
  std::string headline;  // "Dry for the next 2 hours", "Rain from 14:00", "Rain until 14:15", "Rain for the next 2 hours"
  double peak = 0;       // highest rate in the window, mm/h
};
RainSummary rainSummary(const std::vector<RainBlock>& blocks, int64_t now);
// Height in the plot, 0..1, on a log scale like the radar apps: drizzle shows, a downpour does not flatten the rest.
double rainHeight(double rate);
// "<0.1 mm/h", "1.4 mm/h", "12 mm/h".
std::string rateText(double rate);

// sun.sun's state JSON: attributes next_rising and next_setting. False if neither is there.
bool parseSun(const std::string& json, int64_t& sunrise, int64_t& sunset);
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
// Night for the icon: the forecast's is_daytime when it has one, else 19:00-07:00 local.
bool isNight(const Step& s);
// The hours from the one that holds `now` on, grouped per local day (at most `maxDays`) into blocks of `span` hours.
// Hours that are past are left out, so today's first block can be short; a day with no hours left is left out, and so is
// a last block the forecast ends in the middle of.
std::vector<DayBlocks> dayBlocks(const std::vector<Step>& hourly, int64_t now, int span, int maxDays);
// Local date as a day number (days since 1970-01-01) of a unix time, using the process time zone.
int localDay(int64_t t);

}  // namespace pos::weather
