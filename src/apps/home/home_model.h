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
  std::string type;    // light, scene, switch, fan, climate, room, contact, sensor
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

// The saved copy of the last states (/data/cache/home/states.json): shown first, refreshed after.
std::string statesToJson(const States& s);
States statesFromJson(const std::string& json);

// The request body of a service call for an entity: its data object plus "entity_id".
std::string callBody(const Call& call);
// Splits "light.turn_on" into domain and service.
bool splitService(const std::string& service, std::string& domain, std::string& name);

}  // namespace pos::home
