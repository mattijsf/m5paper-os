#include <doctest.h>

#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "apps/weather/weather_model.h"
#include "core/http_fixture.h"
#include "core/timefmt.h"

using namespace pos::weather;

namespace {
std::string readFixture(const std::string& name) {
  std::ifstream f("tests/fixtures/http/" + name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
const int64_t kNow = 1768480440;  // 2026-01-15T12:34:00Z, the UI tests' clock
}  // namespace

TEST_CASE("weather: ISO times with Z, offsets and fractions") {
  int64_t t = 0;
  CHECK(parseIsoTime("2026-01-15T12:34:00Z", t));
  CHECK(t == kNow);
  CHECK(parseIsoTime("2026-01-15T12:34:00+00:00", t));
  CHECK(t == kNow);
  CHECK(parseIsoTime("2026-01-15T13:34:00.123+01:00", t));
  CHECK(t == kNow);
  CHECK(parseIsoTime("2026-01-15T07:34-05:00", t));
  CHECK(t == kNow);
  CHECK_FALSE(parseIsoTime("yesterday", t));
  CHECK_FALSE(parseIsoTime("2026-13-15T12:34:00Z", t));
}

TEST_CASE("weather: the entity's state") {
  Current c;
  REQUIRE(parseState(readFixture("GET_ha.example.test_api_states_weather.thuis.json"), c));
  CHECK(c.condition == "partlycloudy");
  CHECK(c.temperature == doctest::Approx(6.4));
  CHECK(c.apparent == doctest::Approx(2.9));
  CHECK(c.humidity == doctest::Approx(85));
  CHECK(c.windSpeed == doctest::Approx(22));
  CHECK(compass(c.windBearing) == "SW");
  CHECK(c.windUnit == "km/h");
  CHECK_FALSE(parseState(R"({"entity_id":"weather.thuis","state":"unavailable","attributes":{}})", c));
  CHECK_FALSE(parseState("not json", c));
}

TEST_CASE("weather: forecasts from get_forecasts, fixture names per body") {
  const std::string url = "https://ha.example.test/api/services/weather/get_forecasts?return_response";
  const std::string daily = pos::fixtureFileName("POST", url, R"({"entity_id":"weather.thuis","type":"daily"})");
  const std::string hourly = pos::fixtureFileName("POST", url, R"({"entity_id":"weather.thuis","type":"hourly"})");
  CHECK(daily != hourly);
  CHECK(pos::fixtureFileName("POST", url, "") == pos::fixtureFileName("POST", url));
  std::vector<Step> d, h;
  REQUIRE(parseForecast(readFixture(daily), "weather.thuis", d));
  REQUIRE(parseForecast(readFixture(hourly), "weather.thuis", h));
  CHECK(d.size() == 10);
  CHECK(h.size() == 24);
  CHECK(d[0].templow == doctest::Approx(1.4));
  CHECK(d[0].precipProbability == doctest::Approx(80));
  CHECK(h[0].time == kNow - kNow % 3600);
  CHECK(h[0].daytime == 1);
  CHECK_FALSE(parseForecast(readFixture(daily), "weather.other", d));
}

TEST_CASE("weather: saved copy round trip and what is shown when") {
  pos::applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  Weather w;
  parseState(readFixture("GET_ha.example.test_api_states_weather.thuis.json"), w.now);
  const std::string url = "https://ha.example.test/api/services/weather/get_forecasts?return_response";
  parseForecast(readFixture(pos::fixtureFileName("POST", url, R"({"entity_id":"weather.thuis","type":"daily"})")), "weather.thuis", w.daily);
  parseForecast(readFixture(pos::fixtureFileName("POST", url, R"({"entity_id":"weather.thuis","type":"hourly"})")), "weather.thuis", w.hourly);
  w.fetched = kNow;
  Weather back;
  REQUIRE(fromJson(toJson(w), back));
  CHECK(back.fetched == kNow);
  CHECK(back.now.condition == "partlycloudy");
  CHECK(back.now.apparent == doctest::Approx(2.9));
  CHECK(back.hourly.size() == 24);
  CHECK(back.daily[1].condition == w.daily[1].condition);

  // Three hours later the first two hours are gone; two hours apart from the current one.
  auto hours = nextHours(w.hourly, kNow + 3 * 3600, 8, 2);
  REQUIRE(hours.size() == 8);
  CHECK(hours[0].time == kNow - kNow % 3600 + 3 * 3600);
  CHECK(hours[1].time == hours[0].time + 2 * 3600);
  // The next day (local) starts with that day.
  auto days = nextDays(w.daily, kNow + 86400, 6);
  REQUIRE(days.size() == 6);
  CHECK(days[0].time == w.daily[1].time);
  CHECK(nextDays(w.daily, 0, 3).size() == 3);  // no clock: from the first
}

TEST_CASE("weather: today and tomorrow in blocks of three hours") {
  pos::applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  const std::string url = "https://ha.example.test/api/services/weather/get_forecasts?return_response";
  std::vector<Step> h;
  REQUIRE(parseForecast(readFixture(pos::fixtureFileName("POST", url, R"({"entity_id":"weather.thuis","type":"hourly"})")), "weather.thuis", h));
  // 13:34 local: the hours 13:00 today .. 12:00 tomorrow.
  auto days = dayBlocks(h, kNow, 3, 2);
  REQUIRE(days.size() == 2);
  const auto& today = days[0].blocks;
  REQUIRE(today.size() == 4);  // 13-15 (the rest of 12-15), 15-18, 18-21, 21-24
  CHECK(today[0].fromHour == 13);
  CHECK(today[0].toHour == 15);
  CHECK(today[0].slot == 4);
  CHECK(today[0].condition == "partlycloudy");
  CHECK(today[0].temperature == doctest::Approx(6.6));  // 6.4, 6.8
  CHECK(today[0].precipProbability == doctest::Approx(10));
  CHECK_FALSE(today[0].night);
  CHECK(today[1].fromHour == 15);
  CHECK(today[1].toHour == 18);
  CHECK(today[1].condition == "cloudy");  // cloudy, cloudy, rainy
  CHECK(today[1].precipProbability == doctest::Approx(70));
  CHECK(today[1].precipitation == doctest::Approx(0.7));
  CHECK_FALSE(today[1].night);  // 15:00 and 16:00 are day, 17:00 is not (is_daytime)
  CHECK(today[2].night);
  CHECK(today[2].condition == "rainy");  // rainy, pouring, rainy
  CHECK(today[2].precipitation == doctest::Approx(5.1));
  CHECK(today[3].slot == 7);
  CHECK(today[3].toHour == 24);
  const auto& tomorrow = days[1].blocks;
  REQUIRE(tomorrow.size() == 4);  // 0-3 .. 9-12; the one hour of 12-15 the forecast still has is left out
  CHECK(tomorrow[0].slot == 0);
  CHECK(tomorrow[0].condition == "partlycloudy");  // partlycloudy, partlycloudy, clear-night
  CHECK(tomorrow[0].night);
  CHECK(tomorrow[3].fromHour == 9);
  CHECK(tomorrow[3].toHour == 12);
  // A forecast that ends inside its only block of the next day: that day is left out.
  std::vector<Step> short25(h.begin(), h.begin() + 12);  // 13:00 .. 00:00
  auto cut = dayBlocks(short25, kNow, 3, 2);
  REQUIRE(cut.size() == 1);
  CHECK(cut[0].blocks.size() == 4);
  // Two hours later the 13-15 block is gone, and one day is all that is asked for.
  auto later = dayBlocks(h, kNow + 2 * 3600, 3, 1);
  REQUIRE(later.size() == 1);
  CHECK(later[0].blocks.front().fromHour == 15);
  CHECK(dayBlocks({}, kNow, 3, 2).empty());
}

TEST_CASE("weather: the sun's next rise and set") {
  int64_t rise = 1, set = 1;
  REQUIRE(parseSun(readFixture("GET_ha.example.test_api_states_sun.sun.json"), rise, set));
  int64_t want = 0;
  parseIsoTime("2026-01-16T07:41:27Z", want);
  CHECK(rise == want);
  parseIsoTime("2026-01-15T15:58:31Z", want);
  CHECK(set == want);
  int64_t keptRise = rise, keptSet = set;
  CHECK_FALSE(parseSun(R"({"entity_id":"sun.sun","state":"unavailable","attributes":{}})", keptRise, keptSet));
  CHECK(keptRise == rise);  // untouched
  CHECK(keptSet == set);
  Weather w;
  w.now.condition = "sunny";
  w.sunrise = rise;
  w.sunset = set;
  Weather back;
  REQUIRE(fromJson(toJson(w), back));
  CHECK(back.sunrise == rise);
  CHECK(back.sunset == set);
}

TEST_CASE("weather: the minute forecast") {
  pos::applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  std::vector<RainBlock> rain;
  int64_t reported = 0;
  REQUIRE(parseRain(readFixture("GET_ha.example.test_api_states_sensor.google_neerslag_minuten.json"), rain, reported));
  REQUIRE(rain.size() == 24);
  CHECK(reported == kNow - 4 * 60);  // last_reported, not last_updated
  CHECK(rain[0].start == kNow - kNow % 900);
  CHECK(rain[0].end - rain[0].start == 900);
  CHECK(rain[0].rate == doctest::Approx(0));
  CHECK(rain[4].rate == doctest::Approx(4.8));  // 1.2 mm in a quarter of an hour
  CHECK(rain[4].probability == doctest::Approx(60));
  // 13:34: dry now, the shower starts at 14:00.
  RainSummary s = rainSummary(rain, kNow);
  CHECK(s.known);
  CHECK(s.wet);
  CHECK(s.headline == "Rain from 14:00");
  CHECK(s.peak == doctest::Approx(4.8));
  // 14:20: in the shower, dry again from 15:15.
  s = rainSummary(rain, kNow + 46 * 60);
  CHECK(s.headline == "Rain until 15:15");
  // 16:30: nothing in the two hours ahead.
  s = rainSummary(rain, kNow + 176 * 60);
  CHECK(s.known);
  CHECK_FALSE(s.wet);
  CHECK(s.headline == "Dry for the next 2 hours");
  // Rain in every block ahead; and a forecast that is over is not known.
  std::vector<RainBlock> wet = {{kNow - 60, kNow + 3 * 3600, 2.0, 80}};
  CHECK(rainSummary(wet, kNow).headline == "Rain for the next 2 hours");
  CHECK_FALSE(rainSummary(rain, kNow + 7 * 3600).known);
  CHECK_FALSE(rainSummary({}, kNow).known);
  // An unavailable sensor has no segments.
  CHECK_FALSE(parseRain(R"({"entity_id":"sensor.x","state":"unavailable","attributes":{}})", rain, reported));
  // The plot's height and the rate's text.
  CHECK(rainHeight(0) == 0);
  CHECK(rainHeight(0.5) > 0.2);
  CHECK(rainHeight(4.8) < rainHeight(10));
  CHECK(rainHeight(50) == 1);
  CHECK(rateText(0.04) == "<0.1 mm/h");
  CHECK(rateText(4.8) == "4.8 mm/h");
  CHECK(rateText(12.3) == "12 mm/h");
  // The saved copy keeps the blocks.
  Weather w;
  w.now.condition = "rainy";
  w.rain.assign(rain.begin(), rain.begin() + 0);
  parseRain(readFixture("GET_ha.example.test_api_states_sensor.google_neerslag_minuten.json"), w.rain, reported);
  Weather back;
  REQUIRE(fromJson(toJson(w), back));
  REQUIRE(back.rain.size() == 24);
  CHECK(back.rain[4].rate == doctest::Approx(4.8));
  CHECK(back.rain[4].start == w.rain[4].start);
}

TEST_CASE("weather: places from the settings") {
  std::map<std::string, std::string> keys;
  auto lookup = [&](const std::string& key, const std::string& def) {
    auto it = keys.find(key);
    return it == keys.end() ? def : it->second;
  };
  // Nothing set: home with the sun, and the office.
  auto p = placesFrom(lookup);
  REQUIRE(p.size() == 2);
  CHECK(p[0].name == "Thuis");
  CHECK(p[0].entity == "weather.thuis");
  CHECK(p[0].sun);
  CHECK(p[0].rain == "sensor.google_neerslag_minuten");
  CHECK(p[1].rain.empty());
  CHECK(p[1].name == "Office");
  CHECK(p[1].entity == "weather.office");
  CHECK_FALSE(p[1].sun);
  // Another entity for the second place takes its name from the id; a third place; no sun anywhere.
  keys = {{"entity2", "weather.work_site"}, {"entity3", "weather.texel"}, {"name3", "Eiland"}, {"sun", "0"}};
  p = placesFrom(lookup);
  REQUIRE(p.size() == 3);
  CHECK(p[1].name == "Work site");
  CHECK(p[2].name == "Eiland");
  CHECK_FALSE(p[0].sun);
  // An empty entity leaves the place out.
  keys = {{"entity2", ""}};
  p = placesFrom(lookup);
  REQUIRE(p.size() == 1);
  CHECK(p[0].entity == "weather.thuis");
  CHECK(cacheFile("weather.thuis") == "/data/cache/weather/weather.thuis.json");
  CHECK(cacheFile("weather/../x") == "/data/cache/weather/weather_.._x.json");
}

TEST_CASE("weather: texts and icons") {
  CHECK(compass(0) == "N");
  CHECK(compass(350) == "N");
  CHECK(compass(90) == "E");
  CHECK(compass(kNone).empty());
  CHECK(tempText(6.4) == "6\xC2\xB0");
  CHECK(tempText(-2.6) == "-3\xC2\xB0");
  CHECK(tempText(kNone).empty());
  CHECK(amount(0.35, "mm") == "0.3 mm");  // printf rounding of the binary value
  CHECK(amount(11.8, "mm") == "12 mm");
  CHECK(conditionText("partlycloudy") == "Partly cloudy");
  CHECK(conditionText("something-new") == "something-new");
  CHECK(conditionIcon("partlycloudy") == pos::gen::Icon::CloudSun);
  CHECK(conditionIcon("partlycloudy", true) == pos::gen::Icon::CloudMoon);
  CHECK(conditionIcon("exceptional") == pos::gen::Icon::TriangleAlert);
}
