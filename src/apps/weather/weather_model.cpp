#include "apps/weather/weather_model.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "core/timefmt.h"

namespace pos::weather {

namespace {

double num(JsonVariantConst v) { return v.is<double>() ? v.as<double>() : kNone; }
std::string str(JsonVariantConst v) { return v.is<const char*>() ? std::string(v.as<const char*>()) : std::string(); }

bool parseStep(JsonObjectConst o, Step& s) {
  if (!parseIsoTime(str(o["datetime"]), s.time)) return false;
  s.condition = str(o["condition"]);
  s.temperature = num(o["temperature"]);
  s.templow = num(o["templow"]);
  s.precipitation = num(o["precipitation"]);
  s.precipProbability = num(o["precipitation_probability"]);
  if (o["is_daytime"].is<bool>()) s.daytime = o["is_daytime"].as<bool>() ? 1 : 0;
  return true;
}

void putNum(JsonObject o, const char* key, double v) {
  if (v != kNone) o[key] = v;
}

}  // namespace

std::vector<Place> placesFrom(const std::function<std::string(const std::string&, const std::string&)>& lookup) {
  static const struct {
    const char* entity;
    const char* name;
    const char* rain;
  } kDefaults[kMaxPlaces] = {{"weather.thuis", "Thuis", "sensor.google_neerslag_minuten"}, {"weather.office", "Office", ""}, {"", "", ""}, {"", "", ""}};
  int sun = std::atoi(lookup("sun", "1").c_str());
  std::vector<Place> out;
  for (int i = 0; i < kMaxPlaces; ++i) {
    const std::string n = i == 0 ? std::string() : std::to_string(i + 1);
    Place p;
    p.entity = lookup("entity" + n, kDefaults[i].entity);
    if (p.entity.empty()) continue;
    // The default name belongs to the default entity; another entity is named after its id.
    p.name = lookup("name" + n, p.entity == kDefaults[i].entity ? kDefaults[i].name : "");
    if (p.name.empty()) {
      const size_t dot = p.entity.find('.');
      p.name = dot == std::string::npos ? p.entity : p.entity.substr(dot + 1);
      std::replace(p.name.begin(), p.name.end(), '_', ' ');
      if (!p.name.empty() && p.name[0] >= 'a' && p.name[0] <= 'z') p.name[0] = static_cast<char>(p.name[0] - 'a' + 'A');
    }
    p.rain = lookup("rain" + n, p.entity == kDefaults[i].entity ? kDefaults[i].rain : "");
    p.sun = sun == i + 1;
    out.push_back(p);
  }
  return out;
}

std::string cacheFile(const std::string& entity) {
  std::string name;
  for (char c : entity)
    name += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ? c : '_';
  return "/data/cache/weather/" + name + ".json";
}

bool parseIsoTime(const std::string& s, int64_t& out) {
  int y, mo, d, h, mi, sec = 0;
  if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d", &y, &mo, &d, &h, &mi) != 5) return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return false;
  size_t p = 16;
  if (p < s.size() && s[p] == ':') {
    sec = std::atoi(s.c_str() + p + 1);
    p += 3;
  }
  while (p < s.size() && (s[p] == '.' || (s[p] >= '0' && s[p] <= '9'))) ++p;  // fraction of a second
  int offset = 0;
  if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
    int oh = 0, om = 0;
    if (std::sscanf(s.c_str() + p + 1, "%2d:%2d", &oh, &om) < 1) return false;
    offset = (s[p] == '-' ? -1 : 1) * (oh * 3600 + om * 60);
  }
  out = static_cast<int64_t>(daysFromCivil(y, mo, d)) * 86400 + h * 3600 + mi * 60 + sec - offset;
  return true;
}

bool parseState(const std::string& json, Current& out) {
  out = Current{};
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["state"].is<const char*>()) return false;
  out.condition = str(doc["state"]);
  if (out.condition == "unavailable" || out.condition == "unknown") {
    out.condition.clear();
    return false;
  }
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  out.temperature = num(a["temperature"]);
  out.apparent = num(a["apparent_temperature"]);
  out.humidity = num(a["humidity"]);
  out.windSpeed = num(a["wind_speed"]);
  out.windGust = num(a["wind_gust_speed"]);
  out.windBearing = num(a["wind_bearing"]);
  if (a["temperature_unit"].is<const char*>()) out.tempUnit = str(a["temperature_unit"]);
  if (a["wind_speed_unit"].is<const char*>()) out.windUnit = str(a["wind_speed_unit"]);
  if (a["precipitation_unit"].is<const char*>()) out.precipUnit = str(a["precipitation_unit"]);
  return true;
}

bool parseForecast(const std::string& json, const std::string& entity, std::vector<Step>& out) {
  out.clear();
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonArrayConst list = doc["service_response"][entity]["forecast"].as<JsonArrayConst>();
  if (list.isNull()) return false;
  for (JsonObjectConst o : list) {
    Step s;
    if (parseStep(o, s)) out.push_back(s);
  }
  std::sort(out.begin(), out.end(), [](const Step& a, const Step& b) { return a.time < b.time; });
  return true;
}

bool parseRain(const std::string& json, std::vector<RainBlock>& out, int64_t& reported) {
  out.clear();
  reported = 0;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonArrayConst segments = doc["attributes"]["segments"].as<JsonArrayConst>();
  if (segments.isNull()) return false;
  if (!parseIsoTime(str(doc["last_reported"]), reported) && !parseIsoTime(str(doc["last_updated"]), reported)) reported = 0;
  for (JsonObjectConst s : segments) {
    RainBlock b;
    if (!parseIsoTime(str(s["timeFrame"]["startTime"]), b.start) || !parseIsoTime(str(s["timeFrame"]["endTime"]), b.end)) continue;
    if (b.end <= b.start) continue;
    const double mm = num(s["qpf"]["quantity"]);
    b.rate = mm == kNone || mm < 0 ? 0 : mm * 3600.0 / static_cast<double>(b.end - b.start);
    const double p = num(s["probability"]);
    b.probability = p == kNone ? 0 : p;
    out.push_back(b);
  }
  std::sort(out.begin(), out.end(), [](const RainBlock& a, const RainBlock& b) { return a.start < b.start; });
  return true;
}

RainSummary rainSummary(const std::vector<RainBlock>& blocks, int64_t now) {
  RainSummary r;
  const int64_t until = now + kRainWindowSec;
  const RainBlock* current = nullptr;
  const RainBlock* firstWet = nullptr;
  const RainBlock* firstDryAfter = nullptr;  // after the current block
  for (const RainBlock& b : blocks) {
    if (b.end <= now || b.start >= until) continue;
    if (b.start <= now) current = &b;
    r.peak = std::max(r.peak, b.rate);
    const bool wet = b.rate >= kWetRate;
    if (wet && !firstWet) firstWet = &b;
    if (!wet && b.start > now && !firstDryAfter) firstDryAfter = &b;
  }
  r.known = current != nullptr;
  if (!r.known) return r;
  r.wet = firstWet != nullptr;
  if (current->rate >= kWetRate) r.headline = firstDryAfter ? "Rain until " + clockText(firstDryAfter->start) : "Rain for the next 2 hours";
  else r.headline = firstWet ? "Rain from " + clockText(firstWet->start) : "Dry for the next 2 hours";
  return r;
}

double rainHeight(double rate) {
  if (!(rate > 0)) return 0;
  return std::min(1.0, std::log1p(rate * 4) / std::log1p(20.0 * 4));  // the top of the plot is 20 mm/h
}

std::string rateText(double rate) {
  if (rate <= 0) return "0 mm/h";
  if (rate < 0.1) return "<0.1 mm/h";
  char buf[24];
  std::snprintf(buf, sizeof buf, rate < 10 ? "%.1f mm/h" : "%.0f mm/h", rate);
  return buf;
}

bool parseSun(const std::string& json, int64_t& sunrise, int64_t& sunset) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  int64_t rise = 0, set = 0;
  const bool haveRise = parseIsoTime(str(a["next_rising"]), rise);
  const bool haveSet = parseIsoTime(str(a["next_setting"]), set);
  if (!haveRise && !haveSet) return false;
  sunrise = haveRise ? rise : 0;
  sunset = haveSet ? set : 0;
  return true;
}

std::string toJson(const Weather& w) {
  JsonDocument doc;
  doc["fetched"] = w.fetched;
  if (w.sunrise > 0) doc["sunrise"] = w.sunrise;
  if (w.sunset > 0) doc["sunset"] = w.sunset;
  JsonObject n = doc["now"].to<JsonObject>();
  n["condition"] = w.now.condition;
  putNum(n, "temperature", w.now.temperature);
  putNum(n, "apparent", w.now.apparent);
  putNum(n, "humidity", w.now.humidity);
  putNum(n, "wind", w.now.windSpeed);
  putNum(n, "gust", w.now.windGust);
  putNum(n, "bearing", w.now.windBearing);
  n["tempUnit"] = w.now.tempUnit;
  n["windUnit"] = w.now.windUnit;
  n["precipUnit"] = w.now.precipUnit;
  auto steps = [&](const char* key, const std::vector<Step>& list) {
    JsonArray a = doc[key].to<JsonArray>();
    for (const Step& s : list) {
      JsonObject o = a.add<JsonObject>();
      o["t"] = s.time;
      o["c"] = s.condition;
      putNum(o, "temp", s.temperature);
      putNum(o, "low", s.templow);
      putNum(o, "mm", s.precipitation);
      putNum(o, "pct", s.precipProbability);
      if (s.daytime >= 0) o["day"] = s.daytime;
    }
  };
  steps("hourly", w.hourly);
  steps("daily", w.daily);
  if (!w.rain.empty()) {
    JsonArray a = doc["rain"].to<JsonArray>();
    for (const RainBlock& b : w.rain) {
      JsonArray o = a.add<JsonArray>();  // [start, end, rate, probability]
      o.add(b.start);
      o.add(b.end);
      o.add(b.rate);
      o.add(b.probability);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool fromJson(const std::string& json, Weather& out) {
  out = Weather{};
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  out.fetched = doc["fetched"] | static_cast<int64_t>(0);
  out.sunrise = doc["sunrise"] | static_cast<int64_t>(0);
  out.sunset = doc["sunset"] | static_cast<int64_t>(0);
  JsonObjectConst n = doc["now"].as<JsonObjectConst>();
  out.now.condition = str(n["condition"]);
  out.now.temperature = num(n["temperature"]);
  out.now.apparent = num(n["apparent"]);
  out.now.humidity = num(n["humidity"]);
  out.now.windSpeed = num(n["wind"]);
  out.now.windGust = num(n["gust"]);
  out.now.windBearing = num(n["bearing"]);
  if (n["tempUnit"].is<const char*>()) out.now.tempUnit = str(n["tempUnit"]);
  if (n["windUnit"].is<const char*>()) out.now.windUnit = str(n["windUnit"]);
  if (n["precipUnit"].is<const char*>()) out.now.precipUnit = str(n["precipUnit"]);
  auto steps = [&](const char* key, std::vector<Step>& list) {
    for (JsonObjectConst o : doc[key].as<JsonArrayConst>()) {
      Step s;
      s.time = o["t"] | static_cast<int64_t>(0);
      s.condition = str(o["c"]);
      s.temperature = num(o["temp"]);
      s.templow = num(o["low"]);
      s.precipitation = num(o["mm"]);
      s.precipProbability = num(o["pct"]);
      s.daytime = o["day"] | -1;
      list.push_back(s);
    }
  };
  steps("hourly", out.hourly);
  steps("daily", out.daily);
  for (JsonArrayConst o : doc["rain"].as<JsonArrayConst>()) {
    RainBlock b;
    b.start = o[0] | static_cast<int64_t>(0);
    b.end = o[1] | static_cast<int64_t>(0);
    b.rate = o[2] | 0.0;
    b.probability = o[3] | 0.0;
    if (b.end > b.start) out.rain.push_back(b);
  }
  return out.valid();
}

gen::Icon conditionIcon(const std::string& c, bool night) {
  using gen::Icon;
  if (c == "sunny") return night ? Icon::Moon : Icon::Sun;
  if (c == "clear-night") return Icon::Moon;
  if (c == "partlycloudy") return night ? Icon::CloudMoon : Icon::CloudSun;
  if (c == "cloudy") return Icon::Cloud;
  if (c == "rainy") return Icon::CloudRain;
  if (c == "pouring") return Icon::CloudRainWind;
  if (c == "lightning" || c == "lightning-rainy") return Icon::CloudLightning;
  if (c == "snowy" || c == "snowy-rainy") return Icon::CloudSnow;
  if (c == "fog") return Icon::CloudFog;
  if (c == "hail") return Icon::CloudHail;
  if (c == "windy" || c == "windy-variant") return Icon::Wind;
  return Icon::TriangleAlert;  // exceptional, or a condition this version does not know
}

std::string conditionText(const std::string& c) {
  static const struct {
    const char* id;
    const char* text;
  } kTexts[] = {
      {"sunny", "Sunny"},           {"clear-night", "Clear"},          {"partlycloudy", "Partly cloudy"},
      {"cloudy", "Cloudy"},         {"rainy", "Rain"},                 {"pouring", "Heavy rain"},
      {"lightning", "Thunder"},     {"lightning-rainy", "Thunderstorm"}, {"snowy", "Snow"},
      {"snowy-rainy", "Sleet"},     {"fog", "Fog"},                    {"hail", "Hail"},
      {"windy", "Windy"},           {"windy-variant", "Windy, cloudy"}, {"exceptional", "Severe weather"},
  };
  for (const auto& t : kTexts)
    if (c == t.id) return t.text;
  return c;
}

std::string compass(double bearing) {
  if (bearing == kNone) return "";
  static const char* kDirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  double b = std::fmod(bearing, 360.0);
  if (b < 0) b += 360.0;
  return kDirs[static_cast<int>(std::floor(b / 45.0 + 0.5)) % 8];
}

std::string tempText(double v) {
  if (v == kNone) return "";
  long r = std::lround(v);
  return std::to_string(r) + "\xC2\xB0";
}

std::string amount(double v, const std::string& unit) {
  if (v == kNone) return "";
  char buf[24];
  if (v < 10) std::snprintf(buf, sizeof buf, "%.1f %s", v, unit.c_str());
  else std::snprintf(buf, sizeof buf, "%.0f %s", v, unit.c_str());
  return buf;
}

void fillSleepBoard(SleepBoard& board, const Weather& w, int64_t now) {
  if (now <= 0 || w.fetched <= 0 || now - w.fetched > kBoardStaleSec) return;
  const std::vector<Step> days = nextDays(w.daily, now, 1);
  if (days.empty() || localDay(days[0].time) != localDay(now) || days[0].condition.empty()) return;
  const Step& d = days[0];
  board.weather = true;
  board.weatherIcon = conditionIcon(d.condition);
  board.temps = tempText(d.temperature);
  if (d.templow != kNone) board.temps += (board.temps.empty() ? "" : " / ") + tempText(d.templow);
  board.summary = conditionText(d.condition);
  if (d.precipProbability != kNone && d.precipProbability >= 1) {  // "Rain, 80%", "Cloudy, rain 30%"
    const std::string& k = d.condition;  // a condition that says what falls needs no "rain" before the chance
    const bool wet = k.find("rain") != std::string::npos || k == "pouring" || k == "snowy" || k == "hail";
    board.summary += std::string(wet ? ", " : ", rain ") + std::to_string(std::lround(d.precipProbability)) + "%";
  }
  board.blocks.clear();
  for (const DayBlocks& day : dayBlocks(w.hourly, now, 3, 2)) {
    for (const Block& k : day.blocks) {
      if (static_cast<int>(board.blocks.size()) >= kSleepBlocks) return;
      SleepBoard::Block out;
      char range[24];
      std::snprintf(range, sizeof range, "%02d\xE2\x80\x93%02d", k.fromHour, k.toHour);
      out.label = range;
      out.icon = conditionIcon(k.condition, k.night);
      out.temp = tempText(k.temperature);
      if (k.precipProbability != kNone && k.precipProbability >= 1) out.rain = std::to_string(std::lround(k.precipProbability)) + "%";
      board.blocks.push_back(out);
    }
  }
}

int localDay(int64_t t) {
  int days = 0;
  return pos::localDay(t, days) ? days : static_cast<int>(t / 86400);
}

std::vector<Step> nextHours(const std::vector<Step>& hourly, int64_t now, int count, int stride) {
  std::vector<Step> out;
  size_t first = 0;
  if (now > 0)
    while (first < hourly.size() && hourly[first].time + 3600 <= now) ++first;
  for (size_t i = first; i < hourly.size() && static_cast<int>(out.size()) < count; i += static_cast<size_t>(std::max(1, stride)))
    out.push_back(hourly[i]);
  return out;
}

bool isNight(const Step& s) {
  if (s.daytime >= 0) return s.daytime == 0;
  struct tm tmv;
  if (!localTime(s.time, tmv)) return false;
  return tmv.tm_hour < 7 || tmv.tm_hour >= 19;
}

std::vector<DayBlocks> dayBlocks(const std::vector<Step>& hourly, int64_t now, int span, int maxDays) {
  std::vector<DayBlocks> out;
  span = std::max(1, span);
  int day = 0, slot = -1, count = 0, nights = 0;
  double tempSum = 0;
  int temps = 0;
  std::vector<std::pair<std::string, int>> seen;  // condition, hours: in order of first appearance
  auto close = [&] {
    if (count == 0) return;
    Block& b = out.back().blocks.back();
    if (temps > 0) b.temperature = tempSum / temps;
    b.night = 2 * nights > count;
    int best = 0;
    for (const auto& c : seen)
      if (c.second > best) {  // a later condition must have more hours to win
        best = c.second;
        b.condition = c.first;
      }
    count = nights = temps = 0;
    tempSum = 0;
    seen.clear();
  };
  for (const Step& s : hourly) {
    if (now > 0 && s.time + 3600 <= now) continue;
    struct tm tmv;
    if (!localTime(s.time, tmv)) continue;
    const int d = localDay(s.time), sl = tmv.tm_hour / span;
    if (out.empty() || d != day) {
      close();
      if (static_cast<int>(out.size()) >= maxDays) return out;
      out.push_back(DayBlocks{s.time, {}});
      day = d;
      slot = -1;
    }
    if (sl != slot) {
      close();
      Block b;
      b.time = s.time;
      b.fromHour = tmv.tm_hour;
      b.slot = sl;
      out.back().blocks.push_back(b);
      slot = sl;
    }
    Block& b = out.back().blocks.back();
    b.toHour = tmv.tm_hour + 1;
    ++count;
    if (isNight(s)) ++nights;
    if (s.temperature != kNone) {
      tempSum += s.temperature;
      ++temps;
    }
    if (s.precipitation != kNone) b.precipitation = (b.precipitation == kNone ? 0 : b.precipitation) + s.precipitation;
    if (s.precipProbability != kNone) b.precipProbability = std::max(b.precipProbability, s.precipProbability);
    auto it = std::find_if(seen.begin(), seen.end(), [&](const std::pair<std::string, int>& c) { return c.first == s.condition; });
    if (it == seen.end()) seen.emplace_back(s.condition, 1);
    else ++it->second;
  }
  close();
  // The forecast's last block when the forecast ends inside it: an hour or two would stand for three.
  if (!out.empty() && out.back().blocks.back().toHour % span != 0) {
    out.back().blocks.pop_back();
    if (out.back().blocks.empty()) out.pop_back();
  }
  return out;
}

std::vector<Step> nextDays(const std::vector<Step>& daily, int64_t now, int count) {
  std::vector<Step> out;
  const int today = now > 0 ? localDay(now) : -1000000;
  for (const Step& s : daily) {
    if (localDay(s.time) < today) continue;
    if (static_cast<int>(out.size()) >= count) break;
    out.push_back(s);
  }
  return out;
}

}  // namespace pos::weather
