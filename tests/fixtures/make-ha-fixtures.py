#!/usr/bin/env python3
"""Writes the Home Assistant HTTP fixtures of the UI tests (simulator net layer, docs/SIMULATOR.md "HTTP fixtures"): one state
object per entity of the default home.json, plus the service-call answers. Fake data, committed; rerun after changing the table.

  python3 tests/fixtures/make-ha-fixtures.py tests/fixtures/http
"""
import json, os, sys

HOST = "ha.example.test"
LIGHT = lambda on, b=None: ("on" if on else "off", {"brightness": b} if on and b is not None else {})

STATES = {
    # lights: the three Lounge lamps (2 of 3 on), the group reports on
    "light.downstairs_relative": ("on", {"brightness": 153}),
    "light.downstairs": ("on", {"brightness": 153}),
    "light.lounge_relative": ("on", {"brightness": 153}),
    "light.lounge": ("on", {"brightness": 153}),
    "light.sofa_lamp": ("on", {"brightness": 153}),
    "light.coffee_table": ("on", {"brightness": 153}),
    "light.entrance": ("off", {}),
    "light.dining_area_relative": ("off", {}),
    "light.dining_area": ("off", {}),
    "light.dining_table": ("off", {}),
    "light.dining_floor_lamp": ("off", {}),
    "light.reading_corner": ("on", {"brightness": 76}),
    "light.kitchen_spots_relative": ("unavailable", {}),
    "light.kitchen_spots": ("unavailable", {}),
    "light.porch": ("off", {}),
    "light.driveway": ("on", {"brightness": 255}),
    "light.hall": ("off", {}),
    "light.bedside_lamp": ("on", {"brightness": 25}),
    # climate
    "climate.living_room": ("heat", {"current_temperature": 20.5, "temperature": 21.0}),
    "sensor.outdoor_temperature": ("8.3", {"unit_of_measurement": "°C"}),
    "sensor.bedroom_temperature": ("18.9", {"unit_of_measurement": "°C"}),
    "sensor.bedroom_humidity": ("54", {"unit_of_measurement": "%"}),
    "sensor.study_temperature": ("21.4", {"unit_of_measurement": "°C"}),
    "sensor.study_humidity": ("47", {"unit_of_measurement": "%"}),
    "sensor.bathroom_temperature": ("22.8", {"unit_of_measurement": "°C"}),
    "sensor.bathroom_humidity": ("71", {"unit_of_measurement": "%"}),
    "binary_sensor.bedroom_window": ("on", {}),
    # more
    "switch.bathroom_fan": ("off", {}),
    "switch.air_conditioning": ("on", {}),
    "fan.air_purifier": ("off", {}),
}

def name(method, path):
    return f"{method}_{HOST}{path.replace('/', '_')}.json"

def main(out):
    os.makedirs(out, exist_ok=True)
    for eid, (state, attrs) in STATES.items():
        body = {"entity_id": eid, "state": state, "attributes": attrs, "last_changed": "2026-11-15T10:00:00+00:00"}
        with open(os.path.join(out, name("GET", "/api/states/" + eid)), "w") as f:
            json.dump(body, f)
    # service calls: success answers (a list of changed states); the failing ones carry a .status file
    for svc in ["light/turn_on", "light/turn_off", "climate/set_temperature", "fan/turn_on", "fan/turn_off", "switch/turn_on"]:
        with open(os.path.join(out, name("POST", "/api/services/" + svc)), "w") as f:
            f.write("[]")
    p = os.path.join(out, name("POST", "/api/services/switch/turn_off"))
    with open(p, "w") as f:
        f.write('{"message":"boom"}')
    with open(p[:-5] + ".status", "w") as f:
        f.write("500")

main(sys.argv[1])
