# Weather

The weather at home from Home Assistant's **Google Weather** integration (task P7-11). Read only:
the app never changes anything in Home Assistant.

Code: `src/apps/weather/` (`weather_model.*` parsing, saved copy and texts, unit tested in `tests/unit/weather_test.cpp`;
`weather_app.cpp` the screen). Launcher tile "Weather", icon `cloud-sun`, order 98 (after the agenda: page 2).

## Data

| What | Request | Used |
|---|---|---|
| Now | `GET /api/states/weather.thuis` | `state` (condition), `temperature`, `apparent_temperature`, `humidity`, `wind_speed`, `wind_bearing`, units |
| Days | `POST /api/services/weather/get_forecasts?return_response` body `{"entity_id":"weather.thuis","type":"daily"}` | `datetime`, `condition`, `temperature` (high), `templow`, `precipitation`, `precipitation_probability` |
| Hours | the same with `"type":"hourly"` | `datetime`, `condition`, `temperature`, `precipitation`, `precipitation_probability`, `is_daytime` |
| Rain graph | `GET /api/states/sensor.google_neerslag_minuten` | `attributes.segments[]`: `timeFrame.startTime/endTime`, `qpf.quantity` (mm in the block), `probability`; `overallPredictionTimeframe.startTime`, `last_updated` |

- The entity is `weather.thuis` (home-assistant `packages/google_neerslag.yaml` names it as the official Google Weather
  integration's entity; `weather.forecast_thuis_2` in `packages/condens.yaml` is another source). Another entity: key `entity` in
  `/data/apps/weather.json` (no Settings row).
- The forecast attribute on the entity is gone since Home Assistant 2024.3; `weather.get_forecasts` with `?return_response`
  over REST needs 2024.8 or later (the house runs 2026.10). The answer is
  `{"changed_states":[],"service_response":{"weather.thuis":{"forecast":[...]}}}`. Both calls are in the non-admin subset (D18).
- The rain sensor is the house's own REST sensor (home-assistant `packages/google_neerslag.yaml`): Google Weather's minute
  forecast (MetNet nowcast), up to 6 hours in short blocks, read every 10 minutes; the Google Weather integration does not
  fetch it. Another entity: key `rainEntity` in `/data/apps/weather.json`. Missing sensor: the graph says "No current rain forecast".
- Four requests per read, one after the other on the worker over one kept-alive connection (BACKEND.md "Connection").
- The answer shapes in the fixtures follow Home Assistant's weather entity model and the Google Weather integration; they were
  **not yet compared with the live instance** (STATUS.md "Pending device checks").
- Not used: UV, pressure, visibility, gusts, dew point.

## Screen

One screen, no pages (everything fits 540 x 832):

```
| <  Weather                     (refresh) |
| [64 px icon]  6°              ~ SW 22 km/h |
|               Partly cloudy   o 85%        |
|               Feels like 3°   T 10%        |   T = umbrella: rain chance in the current hour (+ mm if any)
|--------------------------------------------|
| T Rain from 14:30     Now 0 mm/h, max 1.2  |   rain graph ("buien"), as the house dashboard's rain card
| Now  14:00   14:30   15:00                 |
| |....................................heavy |
| |....................................moder.|   log scale to 20 mm/h, dotted lines at 0.5 / 2.5 / 10 mm/h
| |__________/####\_____________________light |   tap: next 2 h <-> next 6 h
|--------------------------------------------|
| Now  15:00 17:00 19:00 ... 03:00           |   8 columns, two hours apart (16 hours): icon, temperature, rain %
|--------------------------------------------|
| Today     (icon) 80%  1°  [==bar==]  7°    |   5 rows: today + 4 days; rain % and mm, low, range bar, high
| Tomorrow  ...                              |
|                 Updated 13:34              |   status line
```

- **Rain graph**: a port of the house dashboard's rain card
  (the dashboard `src/app/rain.ts`, `RainCard.tsx`). Rate = block mm scaled to an hour; height on a log scale
  (`log1p(4 rate) / log1p(80)`, clipped at 20 mm/h); a monotone cubic through the middle of each block (no overshoot, never
  below the base), drawn column by column: area gray(6), 2 px ink outline, ink base and "now" line, dotted gray(9) level lines
  with their names right of the plot. Headline like the radar apps: "Dry for the next 2 hours", "Rain from 14:30", "Rain until
  15:15", "Rain for the next 2 hours"; right of it the rate now and the peak in the window. Shown only when fresh: the forecast
  is less than 40 minutes old and reaches past now, else "No current rain forecast" over an empty plot. A tap on the graph
  switches between 2 and 6 hours (only the graph is pushed); a read keeps the choice, leaving the app resets it to 2 hours.
  Not ported: the Thuis card's finger-scrub cursor (dragging is poor on e-ink).
- No section captions ("Next hours", "Next days"): the screen has no room for them since the rain graph.
- The range bar puts each day's low..high on the scale of all six days: track `fill`, range `ink`.
- Rain % is left out under 1 %. The moon replaces the sun for `clear-night`, and for hours with `is_daytime: false` (else
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
| Entity missing or `unavailable` | `EmptyState` "No weather in Home Assistant" + Retry (or the saved copy with that reason) |
| A forecast call failed, the state worked | the new "now" with the saved forecasts; toast "Forecast not available" |

Saved copy: `/data/cache/weather/weather.json` (the parsed values, not the raw answers; about 4 KB).

## Tests

UI scripts (goldens in `tests/ui/golden/`): `weather` (fixtures; then a tap on the graph: `weather-rain-6h`), `weather-offline` (no answer, nothing saved), `weather-stale`
(a copy saved at 07:10, no answer at 13:34), `weather-unset` (no card config). Fixtures: `tests/fixtures/make-weather-fixtures.py`
(writes the four HTTP fixtures and the stale script's saved copy; the rain segments follow the dashboard's mock). The two forecast POSTs share a URL, so their fixtures carry a
body hash (SIMULATOR.md "HTTP fixtures").
