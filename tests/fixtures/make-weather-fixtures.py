#!/usr/bin/env python3
"""Writes the weather HTTP fixtures of the UI tests (docs/apps/weather.md): Home Assistant's Google Weather entity
(weather.thuis), its two forecasts (weather.get_forecasts, daily and hourly) and sun.sun, in the shape Home Assistant 2026.x answers
over REST. Values are made up, January weather in the Netherlands; times relative to the simulator's test clock
(2026-01-15T12:34:00Z = 13:34 in Amsterdam). Also writes the saved copy the stale script starts from, the 404 of an
entity that does not exist (weather.nowhere, the missing script) and the 401 of a host that refuses the token
(ha-denied.example.test, the denied script).

Host ha.example.test (the Home app's test host; the weather entity is the only one of this domain there). The first
place's answers are also written for ha-agenda.example.test, the host with the phone's calendar: the Today board's
script (tests/ui/today-board.script) needs the agenda and the weather from one Home Assistant.

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


def name(method, path, query="", body="", host=HOST):  # src/core/http_fixture.cpp
    n = f"{method}_{host}{path.replace('/', '_')}"
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

# The sun: sets this afternoon (16:58 local), rises tomorrow (08:41 local).
SUN = {
    "entity_id": "sun.sun",
    "state": "above_horizon",
    "attributes": {
        "next_dawn": "2026-01-16T07:03:12.000000+00:00", "next_dusk": "2026-01-15T16:36:40.000000+00:00",
        "next_midnight": "2026-01-15T23:50:11.000000+00:00", "next_noon": "2026-01-16T11:50:23.000000+00:00",
        "next_rising": "2026-01-16T07:41:27.000000+00:00", "next_setting": "2026-01-15T15:58:31.000000+00:00",
        "elevation": 14.2, "azimuth": 192.6, "rising": False, "friendly_name": "Sun",
    },
    "last_changed": iso(NOW - 5 * HOUR), "last_reported": iso(NOW - 2 * 60), "last_updated": iso(NOW - 2 * 60),
    "context": {"id": "01KF0000000000000000000001", "parent_id": None, "user_id": None},
}

# The minute forecast (sensor.google_neerslag_minuten, home-assistant packages/google_neerslag.yaml): Google's
# forecast/minutes:lookup answer kept as attributes. 24 segments of 15 minutes from the last quarter hour (13:30 local).
RAIN = "sensor.google_neerslag_minuten"
QUARTER = 15 * 60


def rain_state(entity, mm):  # mm per segment
    first = NOW - NOW % QUARTER
    segments = []
    for i in range(24):
        q = mm[i] if i < len(mm) else 0
        segments.append({
            "timeFrame": {"startTime": iso(first + i * QUARTER), "endTime": iso(first + (i + 1) * QUARTER)},
            "type": "RAIN" if q > 0 else "NONE", "probability": 60 if q > 0 else 0,
            "qpf": {"unit": "MILLIMETERS", "quantity": q}, "intensity": "LIGHT" if q > 0 else "NO_INTENSITY",
        })
    return {
        "entity_id": entity, "state": "24",
        "attributes": {
            "overallPredictionTimeframe": {"startTime": iso(first), "endTime": iso(first + 6 * HOUR)},
            "segments": segments, "icon": "mdi:weather-pouring", "friendly_name": "Google neerslag minuten",
        },
        "last_changed": iso(NOW - 3 * HOUR), "last_reported": iso(NOW - 4 * 60), "last_updated": iso(NOW - 3 * HOUR),
        "context": {"id": "01KF0000000000000000000002", "parent_id": None, "user_id": None},
    }


# Dry now, a shower from 14:00 that peaks at 14:30 (4.8 mm/h) and is over at 15:15, drizzle again at 15:45.
RAIN_MM = [0, 0, 0.1, 0.4, 1.2, 0.6, 0.1, 0, 0, 0.05, 0.2]

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


def forecast_body(kind, entity=ENTITY):  # what the app sends (weather_app.cpp)
    return '{"entity_id":"%s","type":"%s"}' % (entity, kind)


def response(forecast, entity=ENTITY):
    return {"changed_states": [], "service_response": {entity: {"forecast": forecast}}}


# The second place (weather.office): the same shapes, a little milder, wetter and windier, rain an hour earlier.
OFFICE = "weather.office"


def office_step(step, i):
    s = dict(step)
    for key, delta in (("temperature", 1.3), ("templow", 0.9), ("apparent_temperature", 0.8), ("wind_speed", 6.0)):
        if key in s:
            s[key] = round(s[key] + delta, 1)
    if "precipitation_probability" in s:
        s["precipitation_probability"] = min(100, s["precipitation_probability"] + 10)
    return s


OFFICE_STATE = json.loads(json.dumps(STATE))
OFFICE_STATE["entity_id"] = OFFICE
OFFICE_STATE["state"] = "cloudy"
OFFICE_STATE["attributes"].update({"temperature": 7.7, "apparent_temperature": 3.7, "humidity": 88, "wind_speed": 28.0,
                                   "wind_bearing": 250, "friendly_name": "Office"})
OFFICE_HOURLY = [office_step(dict(HOURLY[min(i + 1, len(HOURLY) - 1)], datetime=HOURLY[i]["datetime"],
                                  is_daytime=HOURLY[i]["is_daytime"]), i) for i in range(len(HOURLY))]
OFFICE_DAILY = [office_step(d, i) for i, d in enumerate(DAILY)]


def write(path, obj, status=0):
    with open(path, "w") as f:
        if isinstance(obj, str):
            f.write(obj)
        else:
            json.dump(obj, f, indent=1, ensure_ascii=False)
            f.write("\n")
    if status:  # a `.status` file next to the fixture holds the status code (docs/SIMULATOR.md "HTTP fixtures")
        with open(path[:-5] + ".status", "w") as f:
            f.write("%d\n" % status)


def main():
    http, data = sys.argv[1], sys.argv[2]
    write(os.path.join(http, name("GET", "/api/states/" + ENTITY)), STATE)
    write(os.path.join(http, name("GET", "/api/states/sun.sun")), SUN)
    write(os.path.join(http, name("GET", "/api/states/" + RAIN)), rain_state(RAIN, RAIN_MM))
    write(os.path.join(http, name("GET", "/api/states/sensor.rain_dry")), rain_state("sensor.rain_dry", []))  # the dry script
    write(os.path.join(http, name("GET", "/api/states/" + OFFICE)), OFFICE_STATE)
    for kind, fc in (("daily", OFFICE_DAILY), ("hourly", OFFICE_HOURLY)):
        write(os.path.join(http, name("POST", "/api/services/weather/get_forecasts", "return_response", forecast_body(kind, OFFICE))),
              response(fc, OFFICE))
    # What Home Assistant answers for an entity it does not have, and for a token it does not accept.
    write(os.path.join(http, name("GET", "/api/states/weather.nowhere")), {"message": "Entity not found."}, 404)
    write(os.path.join(http, name("GET", "/api/states/" + ENTITY, host="ha-denied.example.test")), "401: Unauthorized", 401)
    for kind, fc in (("daily", DAILY), ("hourly", HOURLY)):
        write(os.path.join(http, name("POST", "/api/services/weather/get_forecasts", "return_response", forecast_body(kind))),
              response(fc))
    # The Today board's host: the first place once more (the other place's read fails there, the script never opens it).
    board = "ha-agenda.example.test"
    write(os.path.join(http, name("GET", "/api/states/" + ENTITY, host=board)), STATE)
    write(os.path.join(http, name("GET", "/api/states/sun.sun", host=board)), SUN)
    write(os.path.join(http, name("GET", "/api/states/" + RAIN, host=board)), rain_state(RAIN, RAIN_MM))
    for kind, fc in (("daily", DAILY), ("hourly", HOURLY)):
        write(os.path.join(http, name("POST", "/api/services/weather/get_forecasts", "return_response", forecast_body(kind), host=board)),
              response(fc))
    # The stale script: a copy saved at 07:10 local (06:10Z), no Wi-Fi since. The app's own format (weather_model.cpp toJson).
    saved_at = calendar.timegm((2026, 1, 15, 6, 10, 0))
    first = saved_at - saved_at % HOUR
    stale = {
        "fetched": saved_at,
        "sunrise": calendar.timegm((2026, 1, 15, 7, 42, 20)), "sunset": calendar.timegm((2026, 1, 15, 15, 58, 31)),
        "now": {"condition": "fog", "temperature": 1.8, "apparent": -1.4, "humidity": 97, "wind": 9.0, "gust": 17.0, "bearing": 160,
                "tempUnit": "°C", "windUnit": "km/h", "precipUnit": "mm"},
        "hourly": [{"t": first + i * HOUR, "c": c, "temp": tp, "mm": mm, "pct": p}
                   for i, (c, tp, _, p, mm) in enumerate([("fog", 1.8, 0, 0, 0.0)] * 6 + HOURLY_PLAN[:18])],
        "daily": [{"t": day0 + i * DAY, "c": c, "temp": hi, "low": lo, "mm": mm, "pct": p}
                  for i, (c, hi, lo, p, mm) in enumerate(DAILY_PLAN)],
    }
    d = os.path.join(data, "weather-stale", "data", "cache", "weather")
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, ENTITY + ".json"), "w") as f:
        json.dump(stale, f, separators=(",", ":"))


if __name__ == "__main__":
    main()
