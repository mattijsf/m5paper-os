#include <doctest.h>

#include <cstdlib>
#include <fstream>
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
