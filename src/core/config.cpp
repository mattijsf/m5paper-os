#include "core/config.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "core/fs.h"

namespace pos {

namespace {

template <class T>
T clampTo(T v, T lo, T hi) {
  return std::max(lo, std::min(hi, v));
}

// Reads obj[key] into out if it has the right type.
void readInt(JsonVariantConst v, int& out, int lo, int hi) {
  if (v.is<int>()) out = clampTo(v.as<int>(), lo, hi);
}
void readFloat(JsonVariantConst v, float& out, float lo, float hi) {
  if (v.is<float>()) out = clampTo(v.as<float>(), lo, hi);
}
void readString(JsonVariantConst v, std::string& out) {
  if (v.is<const char*>() && v.as<const char*>()[0]) out = v.as<const char*>();
}
void readEnum(JsonVariantConst v, std::string& out, std::initializer_list<const char*> allowed) {
  if (!v.is<const char*>()) return;
  for (const char* a : allowed)
    if (std::string(a) == v.as<const char*>()) out = a;
}

}  // namespace

void Config::reset() {
  display = DisplayConfig{};
  power = PowerConfig{};
  net = NetConfig{};
  reader = ReaderConfig{};
  ha = HaConfig{};
  launcher = LauncherConfig{};
}

bool Config::load() {
  reset();
  std::string text;
  if (!fs::readFile(path_, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text) || !doc.is<JsonObject>()) return false;

  JsonVariantConst d = doc["display"];
  readFloat(d["ghostLimit"], display.ghostLimit, 4.0f, 16.0f);
  readInt(d["settleMs"], display.settleMs, 100, 3000);
  readInt(d["idleCleanMs"], display.idleCleanMs, 500, 30000);

  JsonVariantConst p = doc["power"];
  readInt(p["autoOffMin"], power.autoOffMin, 0, 240);
  readInt(p["autoOffMinReader"], power.autoOffMinReader, 0, 240);
  if (p["stayAwakeOnUsb"].is<bool>()) power.stayAwakeOnUsb = p["stayAwakeOnUsb"].as<bool>();
  if (p["log"].is<bool>()) power.log = p["log"].as<bool>();

  JsonVariantConst n = doc["net"];
  readInt(n["lingerSec"], net.lingerSec, 0, 600);
  readString(n["timezone"], net.timezone);

  JsonVariantConst r = doc["reader"];
  readEnum(r["fontSize"], reader.fontSize, {"S", "M", "L", "XL"});
  readEnum(r["margins"], reader.margins, {"narrow", "normal", "wide"});
  readFloat(r["lineSpacing"], reader.lineSpacing, 1.1f, 1.8f);

  readString(doc["ha"]["baseUrl"], ha.baseUrl);

  if (doc["launcher"]["resumeLastApp"].is<bool>()) launcher.resumeLastApp = doc["launcher"]["resumeLastApp"].as<bool>();
  return true;
}

bool Config::save() const {
  JsonDocument doc;
  doc["display"]["ghostLimit"] = display.ghostLimit;
  doc["display"]["settleMs"] = display.settleMs;
  doc["display"]["idleCleanMs"] = display.idleCleanMs;
  doc["power"]["autoOffMin"] = power.autoOffMin;
  doc["power"]["autoOffMinReader"] = power.autoOffMinReader;
  doc["power"]["stayAwakeOnUsb"] = power.stayAwakeOnUsb;
  doc["power"]["log"] = power.log;
  doc["net"]["lingerSec"] = net.lingerSec;
  doc["net"]["timezone"] = net.timezone;
  doc["reader"]["fontSize"] = reader.fontSize;
  doc["reader"]["margins"] = reader.margins;
  doc["reader"]["lineSpacing"] = reader.lineSpacing;
  doc["ha"]["baseUrl"] = ha.baseUrl;
  doc["launcher"]["resumeLastApp"] = launcher.resumeLastApp;
  std::string out;
  serializeJsonPretty(doc, out);
  return fs::writeFile(path_, out + "\n");
}

}  // namespace pos
