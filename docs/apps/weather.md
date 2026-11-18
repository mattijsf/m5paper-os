# Weather

The weather at home from Home Assistant's **Google Weather** integration (task P7-11, D29). Read
only: the app never changes anything in Home Assistant.

Code: `src/apps/weather/` (`weather_model.*` parsing, saved copy and texts, unit tested in `tests/unit/weather_test.cpp`;
`weather_fetch.*` the read of one place, shared with the scheduled refresh, and the sleep screen's weather source;
`weather_app.cpp` the screen and its three pages). **Outside the app**: the first place's saved copy is the weather on
the sleep screen's Today board (POWER.md "Today board": today's high and low, the day's condition, the next four blocks;
`fillSleepBoard`), and a scheduled refresh reads that place while the device is off (`refreshSaved`). Launcher tile "Weather", icon `cloud-sun`, order 98 (after the agenda:
page 2).

## Places

The app shows one or more places; each is a weather entity in Home Assistant (in the Google Weather integration every location
is an entity of its own). Default: **Thuis** (`weather.thuis`) and **Office** (`weather.office`; user
request 2026-11-16). With more than one place a `Segmented` switcher stands above the pages.

- Settings in `/data/apps/weather.json` (no Settings row): `entity`, `name`, `rain` (its minute forecast sensor, default
  `sensor.google_neerslag_minuten`) for the first place; `entity2`..`entity4`, `name2`..`name4`, `rain2`..`rain4` for the others (an empty entity leaves a place out; without a name the entity's id is used); `sun` = the
  number of the place `sun.sun` belongs to (default 1, 0: none); `place` = the place shown last (written by the app).
- Each place has its own saved copy, `/data/cache/weather/<entity>.json`. Changing the place shows its copy at once and
  reads it only if that copy is 15 minutes old or more (or missing). The page stays.
- Sunrise and sunset come from `sun.sun`, which is the sun at Home Assistant's own location: shown for Thuis only.
- The office was added in Home Assistant on 2026-11-16 (Settings > Devices & services > Google Weather > "Locatie
  toevoegen": name "Office", a pin near the office, which is closer than the weather is local); its entity is `weather.office`. Before that the Office tab said "No weather in
  Home Assistant" (404). A new place works the same way: add the location there, put its entity in `entity3`.

## Data

| What | Request | Used |
|---|---|---|
| Now | `GET /api/states/weather.thuis` | `state` (condition), `temperature`, `apparent_temperature`, `humidity`, `wind_speed`, `wind_bearing`, units |
| Days | `POST /api/services/weather/get_forecasts?return_response` body `{"entity_id":"weather.thuis","type":"daily"}` | `datetime`, `condition`, `temperature` (high), `templow`, `precipitation`, `precipitation_probability` |
| Hours | the same with `"type":"hourly"` | `datetime`, `condition`, `temperature`, `precipitation`, `precipitation_probability`, `is_daytime` |
| Sun | `GET /api/states/sun.sun` | `next_rising`, `next_setting` |
| Rain, next hours | `GET /api/states/sensor.google_neerslag_minuten` | `attributes.segments[]`: `timeFrame.startTime` / `endTime`, `qpf.quantity` (mm in the segment), `probability`; `last_reported` |

- Thuis is `weather.thuis` (home-assistant `packages/google_neerslag.yaml` names it as the official Google Weather
  integration's entity; `weather.forecast_thuis_2` in `packages/condens.yaml` is another source). The requests below are the
  same for every place, with its entity ("Places").
- The forecast attribute on the entity is gone since Home Assistant 2024.3; `weather.get_forecasts` with `?return_response`
  over REST needs 2024.8 or later (the house runs 2026.10). The answer is
  `{"changed_states":[],"service_response":{"weather.thuis":{"forecast":[...]}}}`. Both calls are in the non-admin subset (D18).
- Five requests per read for Thuis (three for a place without the sun and the rain sensor), one after the other on the worker over one kept-alive connection (BACKEND.md "Connection"). The
  entity's state decides: without it nothing else is asked. A forecast or the sun that fails keeps what was saved.
- The answer shapes in the fixtures follow Home Assistant's weather entity model and the Google Weather integration. Checked
  against the live instance on the device (2026-11-16): the read gives 10 days and 24 hours, every field the app uses is there
  (`is_daytime` too), a read takes about 4 s after the Wi-Fi join.
- The rain sensor is the house's own REST sensor (home-assistant `packages/google_neerslag.yaml`): Google's minute forecast
  (`forecast/minutes:lookup`) for the home zone, read every 10 minutes, segments up to 6 hours ahead. The same data the
  house dashboard's rain graph uses (the rate, the log scale and the headline
  follow it). Only Thuis has one; another place gets one with the key `rain2`..`rain4` in `/data/apps/weather.json`.
- Not used: UV, pressure, visibility, gusts, dew point.

## Screen

Three pages in a `ui::Pager` (swipe left or right, or the arrows of the bar); the page stays when a read comes in.

With one place there is no switcher and the pages are 80 px taller (one more day on the first page).

**1 Now** (goldens `weather`, `weather-office`)

```
| <  Weather                     (refresh) |
| [   Thuis   ][   Office   ]              |   the places
| [64 px icon]  6°            ~ SW 22 km/h |
|               Partly cloudy   o 85%      |
|               Feels like 3°   T 10%      |   T = umbrella: rain chance in the current hour (+ mm if any)
|------------------------------------------|
| Next hours                               |
| Now  15:00 17:00 19:00 ... 03:00         |   8 columns, two hours apart (16 hours): icon, temperature, rain %
|------------------------------------------|
| Next days                                |
| Today     (icon) 80%  1°  [==bar==]  7°  |   4 rows: today + 3 days; rain % and mm, low, range bar, high
| Tomorrow  ...                            |
|  <              1 / 3                 >  |
|               Updated 13:34              |   status line
```

**Rain in the next two hours** (first page, between "now" and the next hours; goldens `weather`, `weather-dry`): only for
a place with a rain sensor, and only while the read is less than 40 minutes old and Home Assistant wrote the sensor less
than 40 minutes before the read (an older forecast is not shown as one).

```
| Rain from 14:00                 max 4.8 mm/h |   or "Rain until 15:15", "Rain for the next 2 hours"
|          ▂▂▂▄▄▄▆▆▆▄▄▄▂▂▂                     |   one bar per segment, ink; height on a log scale, top 20 mm/h
|-------|---------|---------|---------|--------|
|     14:00     14:30     15:00     15:30      |   a tick at every half hour
```

- A segment counts as rain from 0.05 mm/h; its rate is its amount scaled to an hour.
- No rain in the two hours: one line, "Dry for the next 2 hours", and no graph.
- The graph takes the place of day rows: 2 days below it with the place switcher, 3 below the dry line (4 without either).

**2 Hours** (goldens `weather-hours`, `weather-office-hours`): today and tomorrow as **blocks of three hours**.

```
| Today                       (sunset) 16:58 |
| [13–15] [15–18] [18–21] [21–24]            |   a block: hours, icon, temperature, rain %, mm
| Tomorrow                   (sunrise) 08:41 |
| [00–03] [03–06] [06–09] [09–12]            |
```

- Blocks are clock-aligned (00, 03, ... 21 local time), four in a row: the first row of a day is the night and morning, the
  second the afternoon and evening, so a column always means the same time of day. A row that is past is left out; hours
  that are past are left out of today's first block ("13–15" at 13:34).
- A block shows the condition most of its hours have (the earliest of equals), the mean temperature, the highest rain
  chance and the summed amount; the moon when most of its hours are night (`dayBlocks` in `weather_model`, unit tested).
- A last block the forecast ends inside (one hour of 12–15) is left out. With 24 hours of forecast (what Google Weather
  gives) that is three rows at most; of a fourth row (48 hours or more, read before noon) the last would fall off below the
  switcher and is left out.
- The sun's next rise and set (`sun.sun`) stand in the header of the day they fall on.

**3 Days** (golden `weather-days`): all days of the daily forecast (ten), rows like on the first page.

- The range bar puts each day's low..high on the scale of the days on that page: track `fill`, range `ink`.
- Rain % is left out under 1 %, the amount under 0.05 mm (no "0.0 mm"). The moon replaces the sun for `clear-night`, and for hours with `is_daytime: false` (else
  local 19:00-07:00).
- Hours start at the hour that holds now: a saved forecast does not show hours that are past.
- The title bar's refresh button reads again. The app reads when it opens (if the last read is 15 minutes old or more) and
  every 15 minutes while it stays open.
- Texts are English like the rest of the system (conditions: Sunny, Clear, Partly cloudy, Cloudy, Rain, Heavy rain,
  Thunder, Thunderstorm, Snow, Sleet, Fog, Hail, Windy, Windy cloudy, Severe weather).

## States

| Situation | Screen |
|---|---|
| Read worked | the weather; status "Updated 13:34" |
| Read failed, a saved copy exists | the saved weather; status "Saved data from 07:10: <reason>" ("Wed 07:10" on another day). **Stale**: when the saved "now" is more than 90 minutes old, the top block shows the forecast for the current hour ("Forecast for 13:00") and leaves out wind and humidity |
| Read failed, nothing saved | `EmptyState` "Cannot reach Home Assistant" + reason + Retry |
| No `HA_URL` / `HA_TOKEN` | `EmptyState` "Not set up yet" |
| Home Assistant answers 401 / 403, nothing saved | `EmptyState` "Home Assistant refused the token" + "Check HA_TOKEN in paperos_config on the SD card." + Retry (with a saved copy: status "Saved data from 07:10: Token rejected") |
| Entity missing or `unavailable` | `EmptyState` "No weather in Home Assistant" + Retry (or the saved copy with that reason) |
| A forecast call failed, the state worked | the new "now" with the saved forecasts; toast "Forecast not available" |

Saved copy: `/data/cache/weather/<entity>.json` per place (the parsed values, not the raw answers; about 4 KB each). The
copy from before there were places (`weather.json`) is removed when the app opens.

## Tests

UI scripts (goldens in `tests/ui/golden/`): `weather` (fixtures, both places; shots `weather`, `weather-office`,
`weather-office-hours`, `weather-hours`, `weather-days`),
`weather-offline` (no answer, nothing saved), `weather-stale` (a copy saved at 07:10, no answer at 13:34; shots `weather-stale`,
`weather-stale-hours`), `weather-dry` (one place, no switcher, a rain sensor without rain), `weather-unset` (no card config), `weather-denied` (401), `weather-missing` (the second place's entity in
`/data/apps/weather.json` does not exist: 404, Thuis works). Fixtures: `tests/fixtures/make-weather-fixtures.py` (writes the HTTP fixtures:
both places' entity and two forecasts, sun, the rain sensor with a shower and one without, the 404 and the 401, and the
stale script's saved copy, and the first place once more for ha-agenda.example.test, the Today board's host). The two forecast POSTs share a URL, so their fixtures carry a
body hash (SIMULATOR.md "HTTP fixtures").
