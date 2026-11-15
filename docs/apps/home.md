# Home (Home Assistant control)

A remote control for the house: lights, climate, a few switches, music. Much smaller than a Home Assistant
dashboard: what you want from an e-ink remote on the couch or bedside. Which lamps and rooms it shows is a file
(`home.json`, below); the firmware ships an example house and the owner's own list lives on the card.

Backend details (URL, token, API calls) are in `BACKEND.md`.

## Screens

Four tabs (`TabBar`): **Lights**, **Climate**, **More**, **Music**. What appears on each tab is data, not
code: `home.json` (below).

### Lights tab

Rows, 112 px each, one per light or light group:

```
[icon 32]  Lounge                      [ Toggle ]
           3 of 3 on - 60%              LevelBar (10 segments, tap to set)
```

- Toggle: `light.turn_on` / `light.turn_off` on the entity.
- LevelBar: `light.turn_on` with `brightness_pct`. Shown only if `dimmable` is not false.
- Scene buttons (`Button`, secondary) at the top of the tab for entries of type `scene`
  (for example "Evening"): one tap runs the listed service calls.
- Tap on the row text opens a `Sheet` with the group's member lamps (toggle each), if `members`
  is set.

### Climate tab

- Big tiles (2 columns): room name, temperature in `headline`, humidity in caption.
- Thermostat card for `climate` entities: current temperature, target with `Stepper` (0.5
  steps, `climate.set_temperature`), mode.
- Windows/doors: list of open ones ("Bedroom window open"), or "Everything closed".

### More tab

Switches and fans as rows with a Toggle (`switch.turn_on/off`, `fan.turn_on/off`), plus plain
sensor rows (name + value + unit). "Refresh" and "Updated 12:34" at the bottom.

### Music tab (P7-24)

A media player (`media_player.living_room` in the example; built for a Sonos). Laid out after a dashboard's player
sheet:

```
+----------+  Living room music · Playing            (Caption)
|  cover   |  Kings & Queens                        (Title, 3 lines)
| 200x200  |  Ava Max · Heaven & Hell               (Body, secondary, 2 lines)
|          |  Spotify · Fun Road Trip Mix           (Caption: source without "Connect", playlist)
+----------+

 [shuffle]  [prev]  ( PLAY )  [next]  [repeat]       (play: ink disc; shuffle / repeat on: ink ring; repeat one: repeat-1)

Volume                                               ("Volume, muted" when muted)
[mute]  [ -        24%        + ]                    (Stepper, 2 % a tap)
[][][][][][][][][][]                                 (LevelBar: a tap jumps to 10..100 %)
(radio)  Stations                         Spotify    (opens a sheet: TV and the Sonos favourites, two a row)
```

- Cover: `entity_picture` (Home Assistant's media proxy, with the bearer token), decoded on the worker into 200 x 200
  grays, pushed as `Hint::Image` (a Quality pass on the cover only). Read again only when the song changes (title, artist,
  album), not when the proxy's token in the address rotates. The address carries an access token: never logged or saved.
  No picture: a framed box with the music icon.
- Nothing playing (idle, off, standby): "Nothing playing" as the title, the rest empty; play/pause stays (Sonos resumes).
  Playing without a title (the TV input): the source is the title ("TV").
- Play/pause: `media_player.media_play_pause`, the button and the state flip at once. Previous / next:
  `media_previous_track` / `media_next_track`; the new title comes with the next read. Shuffle: `shuffle_set`
  (`shuffle`); repeat: `repeat_set`, off -> all -> one -> off; both flip at once.
- Volume: `media_player.volume_set` (`volume_level` 0..1). The Stepper's value is sent 0.6 s after its last tap (one call for
  a run of taps, as the thermostat); a tap on the ten boxes is sent at once. Mute: `volume_mute` (`is_volume_muted`), the
  icon is `volume-2` or `volume-x` and the caption says "Volume, muted".
- Stations: `source_list` in a Sheet as Buttons, two a row (eight fit without scrolling), the current `source` a Primary
  button; a tap sends `select_source`
  (`source`) and closes the sheet; the title shows the station at once.
- A control the player cannot use (`supported_features`: previous 16, next 32, volume set 4, mute 8, select source 2048,
  shuffle 32768, repeat 262144) is left out; without the attribute everything shows.
- No progress bar or position: it changes while nobody touches the panel.

## As built (simulator, 2026-11-15)

- `apps/home/home_app.cpp`; model and parser in `home_model` (unit-tested), the client in `core/ha`. Tab kinds follow the items: lights and
  scenes (scene buttons on top, 5 rows per page with a Pager), climate/rooms/contacts, switches/fans/sensors.
- **The brightness LevelBar is in the sheet** that opens when the row text of a light is tapped (with the member lamps), not inside the
  row: a 96 px `ListItem` with a Toggle keeps five rows on a page; the sheet is one tap away.
- The light sheet shows "Brightness" with the value on the right ("60%", "--" when not known; it follows the boxes as they are tapped) above the ten boxes.
- The thermostat Stepper counts half degrees; a change is sent 0.8 s after the last tap (`climate.set_temperature`).
- A group toggle updates the stand-in, the read-only group and the members at once (the "x of y on" count follows); a failed call puts
  them all back and shows the error in a toast (`haErrorText`).
- A line above the tab bar says "Updated 13:34", "Saved data: No Wi-Fi", "Loading..." or "n not found in Home Assistant".
- Cache: `/data/cache/home/states.json`, drawn before the first read. Polling: every 30 s while a touch was less than 2 minutes ago;
  on the Music tab every 5 s (2026-11-20: a new song
  or source waited for the 30 s read). A read of the one player is ~40 ms on the kept connection, and an unchanged state draws nothing.
  Not built: extending the Wi-Fi linger during that window (the net service reconnects on the next poll, ~1.3 s fast path).
- Fixtures: `tests/fixtures/make-ha-fixtures.py` writes one state per default entity and the service answers (fake data; the
  `switch/turn_off` answer is a 500 on purpose). Preset card `tests/ui/data/home/sd/paperos_config` holds fake values only.

- **Music tab (P7-24, 2026-11-20)**: `buildMediaCard` / `applyMedia` / `loadCover` / `openStations` and the actions in `home_app.cpp`,
  `mediaView` in `home_model`. A local change (play/pause, mute, volume) shows at once and is put back if the call fails; the tab
  is read 1.5 s after a call and every 5 s while it is shown (above). The real Sonos on its TV input reports `media_title` "TV" and `source` "TV": shown once.

## Behaviour

- `onShow`: draw from `/data/cache/home/states.json` immediately, then fetch fresh states for
  the visible tab (one `GET /api/states/<id>` per entity over a kept-alive connection, see
  BACKEND.md "Reading the Home app's states"), then the other tabs. Update only rows whose
  values changed.
- After a control action: optimistic update (toggle flips at once, `Hint::Feedback`), send the
  service call, then re-fetch states after 1.5 s (lights report their new state with a delay).
  If the call fails: revert the widget, `Toast("Home Assistant not reachable")`.
- While the app is in the foreground and touched within the last 2 minutes: refresh every 30 s,
  keep Wi-Fi up (POWER.md). After that, no polling; the next touch refreshes. Since the idle return (apps/launcher.md, default
  a minute without a touch) the app is left before the two minutes are over, unless that setting is longer.
- No history, charts, cameras or vacuum control. Media: the Music tab (P7-24).

## Config: `/data/home.json`

The default is `src/apps/home/default_config.cpp`: **an example house, not a real one**. Its entity ids exist in no
Home Assistant, so a device shows them as "not found" until the owner's list is on the card: **`/sd/paperos/home.json`**,
same format, never in git (the simulator reads `sim-data/sd/paperos/home.json`). The UI tests and their fixtures use the
example. Rows in order of importance:

| Page | Rows |
|---|---|
| top | scene buttons **Evening** (seven lamps) and **Dining default** (floor lamp pink, table warm) |
| 1 | **Sofa lamp**, Whole living room, Lounge, Dining area, Reading corner |
| 2 | Kitchen spots, **All downstairs** (one switch for every lamp downstairs, kitchen too), Porch, Driveway, Bedside lamp |

Keys beyond v1: `"version"` (the default's is 5; a `/data/home.json` with an older or no version is replaced by the
default on the next start: the internal copy belongs to the firmware, **own edits go in `/sd/paperos/home.json`**, which
always wins and is never replaced); `"names"` (entity -> what the house calls the lamp, shown in a group's sheet: Sofa lamp,
Coffee table, Entrance, ...); per light `"also"` (more stand-ins that get the same on / off: "All downstairs" switches
`light.downstairs_relative` and `light.kitchen_spots_relative`; on while any of its `members` is on, "3 of 7 on" with the
reading corner and the kitchen counted as one lamp each).

Notes on the data:

- A group row can talk to a stand-in entity (`*_relative` in the example: it moves each lamp relative to its own level).
  The plain group (`light.lounge`, ...) is then **read only**: used for "x of y on" and never written. `group` names the
  read-only group; `entity` is what gets commands.
- A scene lists every call it makes (the example's "Evening": seven lamps with brightness and colour).
- An entity that does not exist reads "not found" on its row; nothing else breaks.

The default file ships in the firmware as a string and is written to `/data/home.json` on first
run if the file is missing. Your own house: put a `home.json` in `/sd/paperos/` (it wins over
the internal copy), or push one over USB with `tools/push.py` (task P4-0).

## Item types (v1)

| type | Reads | Commands |
|---|---|---|
| `light` | state, `brightness`, (group members' states for "x of y on") | `light.turn_on` (`brightness_pct`), `light.turn_off` |
| `scene` | nothing | each entry in `calls` |
| `switch` | state | `switch.turn_on` / `turn_off` |
| `fan` | state | `fan.turn_on` / `turn_off` |
| `climate` | `current_temperature`, `temperature`, state | `climate.set_temperature` |
| `room` | temperature / humidity sensor states | none |
| `contact` | binary_sensor state (`on` = open) | none |
| `sensor` | state + `unit_of_measurement` | none |
| `media` | state, `media_title`, `media_artist` (or `media_album_artist`), `media_album_name`, `media_playlist`, `source`, `source_list`, `volume_level`, `is_volume_muted`, `shuffle`, `repeat`, `entity_picture`, `supported_features` | `media_player.media_play_pause`, `media_previous_track`, `media_next_track`, `volume_set`, `volume_mute`, `shuffle_set`, `repeat_set`, `select_source` |

Adding a type = one small class implementing `HomeItem` (`rowHeight()`, `entities()`,
`build(row)`, `apply(states)`), registered in `apps/home/items.cpp`.

## Tests

- Unit: parsing `home.json`; mapping a states response to item view models.
- UI: `tests/ui/home.script` with fixtures (states response, service call OK, service call
  failure). Shots: each tab, a member sheet, the error toast.
- Device check: time from tap to visible toggle change (target under 0.4 s, it is optimistic);
  time from opening the app with Wi-Fi off to fresh data (target under 4 s).
