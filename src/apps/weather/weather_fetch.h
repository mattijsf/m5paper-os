// Reading a place from Home Assistant (docs/apps/weather.md "Calls"), for the weather app and for the scheduled refresh
// (POWER.md): the entity, its two forecasts, the sun and the minute forecast, and the saved copy. Blocking: worker thread.
#pragma once

#include <cstdint>

#include "apps/weather/weather_model.h"
#include "core/ha.h"
#include "core/sleep_screen.h"

namespace pos::weather {

struct Fetched {
  int place = 0;
  HaError error = HaError::None;  // of the entity's state; forecasts that fail keep the saved ones
  bool parsed = false, daily = false, hourly = false;
  Weather weather;
};
// Reads `place` and, when the entity answered, writes its saved copy. `old` is what was known (kept where an answer is
// missing); `now` the clock (unix seconds).
Fetched fetchPlace(HaClient ha, const Place& place, const Weather& old, int64_t now);

// The places of the app's settings (/data/apps/weather.json), as the app reads them; never empty.
std::vector<Place> savedPlaces();
// The scheduled refresh: reads the first place over its saved copy. True if the entity answered.
bool refreshSaved(HaClient ha, int64_t now);
// The sleep screen's weather source (core/sleep_screen.h): the first place's saved copy, no network.
void sleepBoardSource(SleepBoard& board, int64_t now);

}  // namespace pos::weather
