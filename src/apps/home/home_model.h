// Home app model (docs/apps/home.md): the tabs and items from home.json, entity states read from Home Assistant, and what each
// item shows. Pure logic (no widgets, no network): unit-tested. Non-admin API subset only (per-entity state reads, service calls).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "gen/icons.h"

namespace pos::home {

struct Call {
  std::string service;   // "light.turn_on"
  std::string entity;    // entity_id the call is for
  std::string dataJson;  // extra service data as a JSON object ("{}" if none)
};

struct Item {
  std::string type;    // light, scene, switch, fan, climate, room, contact, sensor, media
  std::string name;
  std::string entity;  // what gets commands (light: the *_relative stand-in; climate, switch, fan) or is read (contact, sensor)
  std::string icon;    // name of a gen::Icon, "" = a default for the type
  std::string group;   // read-only group entity for "x of y on"
  bool dimmable = true;
  std::vector<std::string> members;
  std::vector<std::string> also;      // light: more entities that get the same on / off ("All downstairs": the kitchen too)
  std::vector<Call> calls;            // scene
  std::string temperature, humidity;  // room: sensor entity ids
};

struct Tab {
  std::string title, icon;
  std::vector<Item> items;
};

struct Config {
  std::vector<Tab> tabs;
  std::map<std::string, std::string> names;  // "names": what the house calls a lamp ("light.coffee_table": "Coffee table")
};
// The name to show for an entity: from "names", else the id made readable ("light.sofa_lamp" -> "sofa lamp").
std::string nameOf(const Config& c, const std::string& entity);

// Parses home.json. False with `error` set if it is not valid or has no tabs; unknown item types are skipped.
bool parseConfig(const std::string& json, Config& out, std::string& error);
// The firmware's default home.json (the user's house dashboard, trimmed).
const char* defaultConfigJson();
// /sd/paperos/home.json wins over /data/home.json; a missing /data/home.json is written from the default, and so is one whose
// "version" is older than the default's (the internal copy is the firmware's: own edits go on the card). An unreadable file
// falls back to the default and says so in `note`.
int configVersion(const std::string& json);  // "version" of a home.json, 1 when it has none
Config loadConfig(std::string& note);

gen::Icon iconByName(const std::string& name, gen::Icon fallback);
// Every entity id that must be read to show the item (entity, group, members, room sensors).
std::vector<std::string> entitiesOf(const Item& item);
std::vector<std::string> entitiesOf(const Tab& tab);  // unique, in order

struct EntityState {
  bool known = false;      // a state was read
  std::string state;       // "on", "off", "heat", "21.4", "unavailable", ...
  int brightness = -1;     // 0..255, -1 = not reported
  double current = -1000;  // climate current_temperature (-1000 = none)
  double target = -1000;   // climate temperature
  std::string unit;        // unit_of_measurement
  // media_player (docs/apps/home.md "Music tab")
  std::string title, artist, album, playlist, source;
  int volume = -1;         // volume_level in percent, -1 = not reported
  bool muted = false;
  long features = -1;      // supported_features, -1 = not reported (everything shows)
  int shuffle = -1;        // 0 / 1, -1 = not reported
  std::string repeat;      // "off", "all", "one", "" = not reported
  std::vector<std::string> sources;  // source_list (Sonos: TV and the radio favourites)
  std::string picture;     // entity_picture: a path with an access token in it. Never logged, never in the saved copy
  bool unavailable() const { return state == "unavailable" || state == "unknown"; }
  bool on() const { return state == "on"; }
};
using States = std::map<std::string, EntityState>;

// Parses the body of GET /api/states/<id>. False if it is not a state object.
bool parseState(const std::string& json, EntityState& out);

struct LightView {
  bool on = false;
  int percent = -1;   // brightness in percent, -1 unknown
  int onCount = -1;   // lamps on in the group, -1 if there is no group
  int groupSize = 0;
  bool unavailable = false;
};
LightView lightView(const Item& item, const States& states);

// supported_features bits of a media_player (Home Assistant's MediaPlayerEntityFeature).
enum MediaFeature : long {
  kVolumeSet = 4, kVolumeMute = 8, kPreviousTrack = 16, kNextTrack = 32, kSelectSource = 2048, kShuffleSet = 32768, kRepeatSet = 262144
};

struct MediaView {
  bool known = false, unavailable = false;
  bool playing = false, paused = false;
  std::string stateText;  // "Playing", "Paused", "Unavailable", "" (the title says "Nothing playing")
  std::string title;      // the song, the source when there is none (the TV input), "Nothing playing"
  std::string detail;     // artist · album
  std::string origin;     // source (without "Connect") · playlist
  int volume = -1;        // percent, -1 unknown
  bool muted = false;
  bool canPrevious = true, canNext = true, canVolume = true, canMute = true;
  bool shuffle = false;
  std::string repeat = "off";  // off, all, one
  bool canShuffle = false, canRepeat = false, canSource = false;  // shown only when the player reports the state too
  std::vector<std::string> sources;
  std::string source;      // the source as reported ("Spotify Connect"), to tick it in the list
  std::string sourceText;  // as shown ("Spotify"), "" when none
  std::string coverKey;    // what plays (title|artist|album): the cover is read again only when this changes; "" = no cover
  std::string picture;
};
// The next repeat mode: off -> all -> one -> off.
std::string nextRepeat(const std::string& repeat);
MediaView mediaView(const Item& item, const States& states);

// The saved copy of the last states (/data/cache/home/states.json): shown first, refreshed after.
std::string statesToJson(const States& s);
States statesFromJson(const std::string& json);

// The request body of a service call for an entity: its data object plus "entity_id".
std::string callBody(const Call& call);
// Splits "light.turn_on" into domain and service.
bool splitService(const std::string& service, std::string& domain, std::string& name);

}  // namespace pos::home
