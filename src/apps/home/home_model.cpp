#include "apps/home/home_model.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <ArduinoJson.h>

#include "core/fs.h"

namespace pos::home {

namespace {

std::string str(JsonVariantConst v) { return v.is<const char*>() ? std::string(v.as<const char*>()) : std::string(); }

std::string dump(JsonVariantConst v) {
  std::string out;
  serializeJson(v, out);
  return out;
}

bool knownType(const std::string& t) {
  return t == "light" || t == "scene" || t == "switch" || t == "fan" || t == "climate" || t == "room" || t == "contact" || t == "sensor";
}

}  // namespace

bool parseConfig(const std::string& json, Config& out, std::string& error) {
  out = Config{};
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    error = "home.json is not valid JSON";
    return false;
  }
  for (JsonObjectConst t : doc["tabs"].as<JsonArrayConst>()) {
    Tab tab;
    tab.title = str(t["title"]);
    tab.icon = str(t["icon"]);
    if (tab.title.empty()) tab.title = "Tab " + std::to_string(out.tabs.size() + 1);
    for (JsonObjectConst i : t["items"].as<JsonArrayConst>()) {
      Item it;
      it.type = str(i["type"]);
      if (!knownType(it.type)) continue;  // a type this version does not know: skipped, the rest still works
      it.name = str(i["name"]);
      it.entity = str(i["entity"]);
      it.icon = str(i["icon"]);
      it.group = str(i["group"]);
      it.dimmable = i["dimmable"] | true;
      for (JsonVariantConst m : i["members"].as<JsonArrayConst>()) it.members.push_back(str(m));
      it.temperature = str(i["temperature"]);
      it.humidity = str(i["humidity"]);
      for (JsonObjectConst c : i["calls"].as<JsonArrayConst>()) {
        Call call;
        call.service = str(c["service"]);
        call.entity = str(c["entity"]);
        call.dataJson = c["data"].is<JsonObjectConst>() ? dump(c["data"]) : "{}";
        if (!call.service.empty()) it.calls.push_back(std::move(call));
      }
      if (it.name.empty()) it.name = it.entity;
      const bool needsEntity = it.type != "scene" && it.type != "room";
      if ((needsEntity && it.entity.empty()) || (it.type == "scene" && it.calls.empty()) || (it.type == "room" && it.temperature.empty())) continue;
      tab.items.push_back(std::move(it));
    }
    out.tabs.push_back(std::move(tab));
  }
  if (out.tabs.empty()) {
    error = "home.json has no tabs";
    return false;
  }
  return true;
}

Config loadConfig(std::string& note) {
  note.clear();
  Config cfg;
  std::string err, text;
  for (const char* path : {"/sd/paperos/home.json", "/data/home.json"}) {
    if (!fs::readFile(path, text)) continue;
    if (parseConfig(text, cfg, err)) return cfg;
    note = std::string(path) + ": " + err + "; using the default";
    break;
  }
  const bool haveFile = !note.empty();
  parseConfig(defaultConfigJson(), cfg, err);
  if (!haveFile && !fs::exists("/data/home.json")) fs::writeFile("/data/home.json", defaultConfigJson());
  return cfg;
}

gen::Icon iconByName(const std::string& name, gen::Icon fallback) {
  struct Pair {
    const char* n;
    gen::Icon i;
  };
  static const Pair kIcons[] = {{"house", gen::Icon::House},       {"lightbulb", gen::Icon::Lightbulb}, {"lightbulb-off", gen::Icon::LightbulbOff},
                                {"sofa", gen::Icon::Sofa},         {"utensils", gen::Icon::Utensils},   {"blocks", gen::Icon::Blocks},
                                {"cooking-pot", gen::Icon::CookingPot}, {"lamp-wall-down", gen::Icon::LampWallDown}, {"lamp-wall-up", gen::Icon::LampWallUp},
                                {"car", gen::Icon::Car},           {"bed", gen::Icon::Bed},             {"thermometer", gen::Icon::Thermometer},
                                {"droplets", gen::Icon::Droplets}, {"fan", gen::Icon::Fan},             {"wind", gen::Icon::Wind},
                                {"settings", gen::Icon::Settings}, {"air-vent", gen::Icon::AirVent},    {"door-open", gen::Icon::DoorOpen},
                                {"moon", gen::Icon::Moon},         {"sun", gen::Icon::Sun},             {"power", gen::Icon::Power}};
  for (const Pair& p : kIcons)
    if (name == p.n) return p.i;
  return fallback;
}

std::vector<std::string> entitiesOf(const Item& item) {
  std::vector<std::string> out;
  auto add = [&out](const std::string& e) {
    if (!e.empty() && std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
  };
  if (item.type != "scene") add(item.entity);
  add(item.group);
  for (const std::string& m : item.members) add(m);
  add(item.temperature);
  add(item.humidity);
  return out;
}

std::vector<std::string> entitiesOf(const Tab& tab) {
  std::vector<std::string> out;
  for (const Item& it : tab.items)
    for (const std::string& e : entitiesOf(it))
      if (std::find(out.begin(), out.end(), e) == out.end()) out.push_back(e);
  return out;
}

bool parseState(const std::string& json, EntityState& out) {
  out = EntityState{};
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["state"].is<const char*>()) return false;
  out.known = true;
  out.state = str(doc["state"]);
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  if (a["brightness"].is<int>()) out.brightness = a["brightness"].as<int>();
  if (a["current_temperature"].is<double>()) out.current = a["current_temperature"].as<double>();
  if (a["temperature"].is<double>()) out.target = a["temperature"].as<double>();
  out.unit = str(a["unit_of_measurement"]);
  return true;
}

LightView lightView(const Item& item, const States& states) {
  LightView v;
  auto it = states.find(item.entity);
  if (it != states.end() && it->second.known) {
    v.unavailable = it->second.unavailable();
    v.on = it->second.on();
    if (it->second.brightness >= 0) v.percent = (it->second.brightness * 100 + 127) / 255;
  }
  if (!item.group.empty() || !item.members.empty()) {
    const std::vector<std::string>& ids = item.members;
    int on = 0, n = 0;
    for (const std::string& m : ids) {
      auto s = states.find(m);
      ++n;
      if (s != states.end() && s->second.on()) ++on;
    }
    if (n > 0) {
      v.onCount = on;
      v.groupSize = n;
    } else {  // a group without a member list: its own state is all there is
      auto g = states.find(item.group);
      if (g != states.end()) v.onCount = g->second.on() ? 1 : 0, v.groupSize = 1;
    }
    // The stand-in entity of a group may not report its state: the read-only group does.
    auto g = states.find(item.group);
    if (g != states.end() && g->second.known && (it == states.end() || !it->second.known)) {
      v.on = g->second.on();
      if (g->second.brightness >= 0) v.percent = (g->second.brightness * 100 + 127) / 255;
    }
  }
  return v;
}

std::string statesToJson(const States& s) {
  JsonDocument doc;
  for (const auto& kv : s) {
    if (!kv.second.known) continue;
    JsonObject o = doc[kv.first].to<JsonObject>();
    o["s"] = kv.second.state;
    if (kv.second.brightness >= 0) o["b"] = kv.second.brightness;
    if (kv.second.current > -999) o["c"] = kv.second.current;
    if (kv.second.target > -999) o["t"] = kv.second.target;
    if (!kv.second.unit.empty()) o["u"] = kv.second.unit;
  }
  return dump(doc);
}

States statesFromJson(const std::string& json) {
  States out;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return out;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    EntityState e;
    e.known = true;
    e.state = str(kv.value()["s"]);
    e.brightness = kv.value()["b"] | -1;
    e.current = kv.value()["c"] | -1000.0;
    e.target = kv.value()["t"] | -1000.0;
    e.unit = str(kv.value()["u"]);
    out[kv.key().c_str()] = e;
  }
  return out;
}

std::string callBody(const Call& call) {
  JsonDocument doc;
  if (deserializeJson(doc, call.dataJson) || !doc.is<JsonObject>()) doc.to<JsonObject>();
  doc["entity_id"] = call.entity;
  return dump(doc);
}

bool splitService(const std::string& service, std::string& domain, std::string& name) {
  const size_t dot = service.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= service.size()) return false;
  domain = service.substr(0, dot);
  name = service.substr(dot + 1);
  return true;
}

}  // namespace pos::home
