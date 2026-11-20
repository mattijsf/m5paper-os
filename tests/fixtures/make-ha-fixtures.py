#!/usr/bin/env python3
"""Writes the Home Assistant HTTP fixtures of the UI tests (simulator net layer, docs/SIMULATOR.md "HTTP fixtures"): one state
object per entity of the default home.json, plus the service-call answers. Fake data, committed; rerun after changing the table.

  python3 tests/fixtures/make-ha-fixtures.py tests/fixtures/http
"""
import json, os, sys

HOST = "ha.example.test"
COVER = "/api/media_player_proxy/media_player.living_room?token=faketoken&cache=0a1b2c"  # the cover: a made-up picture below
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
    # music: the Sonos in the living room, as the house dashboard's mock has it
    "media_player.living_room": ("playing", {"volume_level": 0.24, "is_volume_muted": False, "media_title": "Voices In My Head",
                                           "media_artist": "Sonata Arctica", "media_album_name": "Reckoning Night",
                                           "media_playlist": "Metalmix", "source": "Spotify Connect", "supported_features": 8321599,
                                           "shuffle": True, "repeat": "all",
                                           "source_list": ["TV", "KINK", "NPO Radio 2", "Radio 10", "Qmusic", "Radio 538",
                                                           "Radio Veronica", "Slam! FM"],
                                           "entity_picture": COVER}),
}

def fnv1a(text):
    h = 2166136261
    for b in text.encode():
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h

def name(method, path):
    """src/core/http_fixture.cpp: '/' becomes '_', a query adds _<FNV-1a of it>."""
    path, _, query = path.partition("?")
    return f"{method}_{HOST}{path.replace('/', '_')}" + (f"_{fnv1a(query):08x}" if query else "") + ".json"

def cover_png(size=300):
    """A made-up cover (no real album art in the repository): rings and a diagonal in grays, as a grayscale PNG."""
    import struct, zlib
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            dx, dy = x - size * 0.42, y - size * 0.45
            r = (dx * dx + dy * dy) ** 0.5
            v = 30 if r < size * 0.08 else (235 if int(r / (size * 0.06)) % 2 else 90) if r < size * 0.38 else 40 + (x + y) * 160 // (2 * size)
            row.append(v)
        rows.append(bytes(row))
    raw = zlib.compress(b"".join(rows), 9)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)) + chunk(b"IDAT", raw) + chunk(b"IEND", b"")

def main(out):
    os.makedirs(out, exist_ok=True)
    for eid, (state, attrs) in STATES.items():
        body = {"entity_id": eid, "state": state, "attributes": attrs, "last_changed": "2026-11-15T10:00:00+00:00"}
        with open(os.path.join(out, name("GET", "/api/states/" + eid)), "w") as f:
            json.dump(body, f)
    with open(os.path.join(out, name("GET", COVER)), "wb") as f:
        f.write(cover_png())
    # service calls: success answers (a list of changed states); the failing ones carry a .status file
    for svc in ["light/turn_on", "light/turn_off", "climate/set_temperature", "fan/turn_on", "fan/turn_off", "switch/turn_on",
                "media_player/media_play_pause", "media_player/media_next_track", "media_player/media_previous_track",
                "media_player/volume_set", "media_player/volume_mute",
                "media_player/shuffle_set", "media_player/repeat_set", "media_player/select_source"]:
        with open(os.path.join(out, name("POST", "/api/services/" + svc)), "w") as f:
            f.write("[]")
    p = os.path.join(out, name("POST", "/api/services/switch/turn_off"))
    with open(p, "w") as f:
        f.write('{"message":"boom"}')
    with open(p[:-5] + ".status", "w") as f:
        f.write("500")

main(sys.argv[1])
