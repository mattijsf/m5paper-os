#!/usr/bin/env python3
"""Writes the weather HTTP fixtures of the UI tests (docs/apps/weather.md): Home Assistant's Google Weather entity
(weather.thuis) and its two forecasts (weather.get_forecasts, daily and hourly), in the shape Home Assistant 2026.x answers
over REST. Values are made up, January weather in the Netherlands; times relative to the simulator's test clock
(2026-01-15T12:34:00Z = 13:34 in Amsterdam). Also Google's minute rain forecast as the house's REST sensor
sensor.google_neerslag_minuten (home-assistant packages/google_neerslag.yaml; segment shape as the the dashboard
mock: timeFrame, type, probability, qpf, intensity) and the saved copy the stale script starts from.

Host ha.example.test (the Home app's test host; the weather entity is the only one of this domain there).

  python3 -I tests/fixtures/make-weather-fixtures.py tests/fixtures/http tests/ui/data
"""
import calendar, json, os, sys, time

NOW = calendar.timegm((2026, 1, 15, 12, 34, 0))
HOUR, DAY = 3600, 86400
HOST = "ha.example.test"
ENTITY = "weather.thuis"


def fnv(text):
    h = 2166136261
    for c in text.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return "%08x" % h


def name(method, path, query="", body=""):  # src/core/http_fixture.cpp
    n = f"{method}_{HOST}{path.replace('/', '_')}"
    if query:
        n += "_" + fnv(query)
    if body:
        n += "_b" + fnv(body)
    return n + ".json"


def iso(t):
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(t))


STATE = {
    "entity_id": ENTITY,
    "state": "partlycloudy",
    "attributes": {
        "temperature": 6.4, "apparent_temperature": 2.9, "dew_point": 4.1, "temperature_unit": "°C",
        "humidity": 85, "cloud_coverage": 62, "uv_index": 1, "pressure": 1011.8, "pressure_unit": "hPa",
        "wind_bearing": 225, "wind_gust_speed": 41.0, "wind_speed": 22.0, "wind_speed_unit": "km/h",
        "visibility": 16.0, "visibility_unit": "km", "precipitation_unit": "mm",
        "attribution": "Data from Google Weather", "friendly_name": "Thuis", "supported_features": 7,
    },
    "last_changed": iso(NOW - 40 * 60), "last_reported": iso(NOW - 4 * 60), "last_updated": iso(NOW - 4 * 60),
    "context": {"id": "01KF0000000000000000000000", "parent_id": None, "user_id": None},
}

# Hourly from the current hour (12:00Z = 13:00 local): clouds, rain from late afternoon into the night, dry by morning.
HOURLY_PLAN = [  # condition, temp, apparent, precip %, mm
    ("partlycloudy", 6.4, 2.9, 10, 0.0), ("partlycloudy", 6.8, 3.4, 10, 0.0), ("cloudy", 6.9, 3.6, 20, 0.0),
    ("cloudy", 6.5, 3.1, 40, 0.1), ("rainy", 6.1, 2.4, 70, 0.6), ("rainy", 5.8, 2.0, 80, 1.2),
    ("pouring", 5.6, 1.7, 90, 2.8), ("rainy", 5.4, 1.6, 80, 1.1), ("rainy", 5.1, 1.5, 60, 0.4),
    ("cloudy", 4.8, 1.4, 30, 0.1), ("cloudy", 4.4, 1.0, 20, 0.0), ("partlycloudy", 3.9, 0.6, 10, 0.0),
    ("partlycloudy", 3.4, 0.1, 10, 0.0), ("clear-night", 2.8, -0.8, 0, 0.0), ("clear-night", 2.3, -1.2, 0, 0.0),
    ("clear-night", 1.9, -1.6, 0, 0.0), ("fog", 1.6, -1.8, 0, 0.0), ("fog", 1.4, -2.0, 0, 0.0),
    ("fog", 1.5, -1.7, 0, 0.0), ("partlycloudy", 2.6, -0.4, 0, 0.0), ("sunny", 3.9, 1.2, 0, 0.0),
    ("sunny", 5.0, 2.5, 0, 0.0), ("partlycloudy", 5.8, 3.3, 10, 0.0), ("partlycloudy", 6.1, 3.5, 10, 0.0),
]
hour0 = NOW - NOW % HOUR
HOURLY = []
for i, (cond, temp, app, pct, mm) in enumerate(HOURLY_PLAN):
    t = hour0 + i * HOUR
    local_hour = (time.gmtime(t).tm_hour + 1) % 24
    HOURLY.append({
        "condition": cond, "datetime": iso(t), "wind_bearing": 225 - i * 4, "cloud_coverage": 60 if "cloud" in cond else 20,
        "uv_index": 1 if 8 <= local_hour <= 16 else 0, "temperature": temp, "apparent_temperature": app,
        "dew_point": round(temp - 2.2, 1), "pressure": 1011.8 - i * 0.3, "wind_gust_speed": 41.0 - i, "wind_speed": 22.0 - i * 0.5,
        "precipitation": mm, "precipitation_probability": pct, "humidity": 85 + (i % 5), "is_daytime": 8 <= local_hour < 17,
    })

DAILY_PLAN = [  # condition, high, low, precip %, mm
    ("rainy", 7.0, 1.4, 80, 6.2), ("partlycloudy", 6.2, 1.1, 10, 0.0), ("cloudy", 5.4, 2.0, 30, 0.3),
    ("rainy", 8.1, 4.2, 70, 4.5), ("pouring", 9.3, 6.0, 90, 11.8), ("sunny", 4.0, -2.1, 0, 0.0),
    ("snowy", 1.2, -3.4, 60, 2.0), ("cloudy", 3.0, -0.5, 20, 0.0), ("partlycloudy", 4.6, 0.2, 10, 0.0),
    ("rainy", 6.7, 2.8, 70, 3.1),
]
day0 = NOW - NOW % DAY + 6 * HOUR  # Google's day starts at 07:00 local
DAILY = []
for i, (cond, hi, lo, pct, mm) in enumerate(DAILY_PLAN):
    DAILY.append({
        "condition": cond, "wind_bearing": 220 + i * 7, "datetime": iso(day0 + i * DAY), "cloud_coverage": 70 if cond != "sunny" else 10,
        "uv_index": 1, "temperature": hi, "templow": lo, "apparent_temperature": round(hi - 3.1, 1),
        "precipitation_probability": pct, "precipitation": mm, "humidity": 84, "wind_speed": 18.0 + i, "wind_gust_speed": 35.0 + i,
    })


# 24 blocks of 15 minutes from 13:30 local: a shower 14:30-15:15, dry, rain from 16:30 building to ~3 mm/h.
RAIN_MM = [0, 0, 0, 0, 0.05, 0.3, 0.1, 0, 0, 0, 0, 0, 0.02, 0.1, 0.2, 0.4, 0.6, 0.7, 0.5, 0.4, 0.3, 0.25, 0.2, 0.15]
rain0 = NOW - NOW % (15 * 60)


def rain_segment(start, mm):
    return {
        "timeFrame": {"startTime": iso(start), "endTime": iso(start + 15 * 60)},
        "type": "RAIN" if mm > 0 else "NONE", "probability": 70 if mm > 0 else 0,
        "qpf": {"unit": "MILLIMETERS", "quantity": mm},
        "intensity": ("MODERATE" if mm >= 0.6 else "LIGHT") if mm > 0 else "NO_INTENSITY",
    }


RAIN = {
    "entity_id": "sensor.google_neerslag_minuten",
    "state": str(len(RAIN_MM)),
    "attributes": {
        "overallPredictionTimeframe": {"startTime": iso(rain0), "endTime": iso(rain0 + 6 * HOUR)},
        "segments": [rain_segment(rain0 + i * 15 * 60, mm) for i, mm in enumerate(RAIN_MM)],
        "icon": "mdi:weather-pouring", "friendly_name": "Google neerslag minuten",
    },
    "last_changed": iso(rain0), "last_reported": iso(NOW - 4 * 60), "last_updated": iso(rain0),
    "context": {"id": "01KF0000000000000000000001", "parent_id": None, "user_id": None},
}


def forecast_body(kind):  # what the app sends (weather_app.cpp)
    return '{"entity_id":"%s","type":"%s"}' % (ENTITY, kind)


def response(forecast):
    return {"changed_states": [], "service_response": {ENTITY: {"forecast": forecast}}}


def write(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f, indent=1, ensure_ascii=False)
        f.write("\n")


def main():
    http, data = sys.argv[1], sys.argv[2]
    write(os.path.join(http, name("GET", "/api/states/" + ENTITY)), STATE)
    write(os.path.join(http, name("GET", "/api/states/sensor.google_neerslag_minuten")), RAIN)
    for kind, fc in (("daily", DAILY), ("hourly", HOURLY)):
        write(os.path.join(http, name("POST", "/api/services/weather/get_forecasts", "return_response", forecast_body(kind))),
              response(fc))
    # The stale script: a copy saved at 07:10 local (06:10Z), no Wi-Fi since. The app's own format (weather_model.cpp toJson).
    saved_at = calendar.timegm((2026, 1, 15, 6, 10, 0))
    first = saved_at - saved_at % HOUR
    stale = {
        "fetched": saved_at,
        "now": {"condition": "fog", "temperature": 1.8, "apparent": -1.4, "humidity": 97, "wind": 9.0, "gust": 17.0, "bearing": 160,
                "tempUnit": "°C", "windUnit": "km/h", "precipUnit": "mm"},
        "hourly": [{"t": first + i * HOUR, "c": c, "temp": tp, "mm": mm, "pct": p}
                   for i, (c, tp, _, p, mm) in enumerate([("fog", 1.8, 0, 0, 0.0)] * 6 + HOURLY_PLAN[:18])],
        "daily": [{"t": day0 + i * DAY, "c": c, "temp": hi, "low": lo, "mm": mm, "pct": p}
                  for i, (c, hi, lo, p, mm) in enumerate(DAILY_PLAN)],
        "rainUpdated": saved_at - saved_at % (15 * 60),
        "rain": [[saved_at - saved_at % (15 * 60) + i * 900, saved_at - saved_at % (15 * 60) + (i + 1) * 900, mm * 4, 70 if mm else 0]
                 for i, mm in enumerate(RAIN_MM)],
    }
    d = os.path.join(data, "weather-stale", "data", "cache", "weather")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, "weather.json"), "w") as f:
        json.dump(stale, f, separators=(",", ":"))


if __name__ == "__main__":
    main()
