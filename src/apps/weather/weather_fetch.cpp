#include "apps/weather/weather_fetch.h"

#include <string>
#include <vector>

#include "core/fs.h"
#include "core/store.h"

namespace pos::weather {

Fetched fetchPlace(HaClient ha, const Place& place, const Weather& old, int64_t now) {
  Fetched f;
  const std::string& entity = place.entity;
  HaResult st = ha.get("/api/states/" + entity);
  f.error = st.error;
  if (!st.ok()) return f;
  f.weather = old;
  if (!parseState(st.body, f.weather.now)) {
    f.error = HaError::NotFound;  // no weather entity, or it is unavailable
    return f;
  }
  f.parsed = true;
  f.weather.fetched = now;
  const std::string body = "{\"entity_id\":\"" + entity + "\",\"type\":\"";
  std::vector<Step> steps;
  HaResult daily = ha.callService("weather", "get_forecasts", body + "daily\"}", true);
  if (daily.ok() && parseForecast(daily.body, entity, steps)) {
    f.weather.daily = steps;
    f.daily = true;
  }
  HaResult hourly = ha.callService("weather", "get_forecasts", body + "hourly\"}", true);
  if (hourly.ok() && parseForecast(hourly.body, entity, steps)) {
    f.weather.hourly = steps;
    f.hourly = true;
  }
  // The sun's next rise and set (sun.sun: Home Assistant's own location, so only for the place that is there);
  // without an answer the saved times stay.
  if (place.sun) {
    HaResult sun = ha.get("/api/states/sun.sun");
    if (sun.ok()) parseSun(sun.body, f.weather.sunrise, f.weather.sunset);
  } else {
    f.weather.sunrise = f.weather.sunset = 0;
  }
  // The minute forecast of the place, if it has one: only a fresh one is kept, and only what is still ahead.
  f.weather.rain.clear();
  if (!place.rain.empty()) {
    HaResult rain = ha.get("/api/states/" + place.rain);
    std::vector<RainBlock> blocks;
    int64_t reported = 0;
    if (rain.ok() && parseRain(rain.body, blocks, reported) && (reported <= 0 || now <= 0 || now - reported <= kRainStaleSec)) {
      for (const RainBlock& b : blocks)
        if (b.end > now && b.start < now + 6 * 3600 && f.weather.rain.size() < 96) f.weather.rain.push_back(b);
    }
  }
  fs::writeFile(cacheFile(entity), toJson(f.weather));
  return f;
}

std::vector<Place> savedPlaces() {
  Store store("/data/apps/weather.json");
  std::vector<Place> places = placesFrom([&store](const std::string& key, const std::string& def) {
    if (key == "sun") return std::to_string(store.get("sun", 1));
    return store.get(key, def.c_str());
  });
  if (places.empty()) places.push_back(Place{"Thuis", "weather.thuis", "", true});
  return places;
}

bool refreshSaved(HaClient ha, int64_t now) {
  const Place place = savedPlaces().front();
  Weather old;
  std::string cached;
  if (fs::readFile(cacheFile(place.entity), cached)) fromJson(cached, old);
  return fetchPlace(ha, place, old, now).parsed;
}

void sleepBoardSource(SleepBoard& board, int64_t now) {
  Weather w;
  std::string cached;
  if (!fs::readFile(cacheFile(savedPlaces().front().entity), cached) || !fromJson(cached, w)) return;
  fillSleepBoard(board, w, now);
}

}  // namespace pos::weather
