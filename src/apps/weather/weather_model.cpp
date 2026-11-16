#include "apps/weather/weather_model.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/timefmt.h"

namespace pos::weather {

namespace {

double num(JsonVariantConst v) { return v.is<double>() ? v.as<double>() : kNone; }
std::string str(JsonVariantConst v) { return v.is<const char*>() ? std::string(v.as<const char*>()) : std::string(); }

// Days since 1970-01-01 of a civil date (Howard Hinnant's days_from_civil).
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

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
  out = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + sec - offset;
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

std::string toJson(const Weather& w) {
  JsonDocument doc;
  doc["fetched"] = w.fetched;
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
  doc["rainUpdated"] = w.rainUpdated;
  JsonArray r = doc["rain"].to<JsonArray>();
  for (const RainBlock& b : w.rain) {
    JsonArray a = r.add<JsonArray>();
    a.add(b.start);
    a.add(b.end);
    a.add(b.rate);
    a.add(b.probability);
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
  out.rainUpdated = doc["rainUpdated"] | static_cast<int64_t>(0);
  for (JsonArrayConst a : doc["rain"].as<JsonArrayConst>()) {
    RainBlock b;
    b.start = a[0] | static_cast<int64_t>(0);
    b.end = a[1] | static_cast<int64_t>(0);
    b.rate = a[2] | 0.0;
    b.probability = a[3] | 0;
    if (b.end > b.start) out.rain.push_back(b);
  }
  return out.valid();
}

bool parseRain(const std::string& json, std::vector<RainBlock>& out, int64_t& updated) {
  out.clear();
  updated = 0;
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  for (JsonObjectConst seg : a["segments"].as<JsonArrayConst>()) {
    RainBlock b;
    if (!parseIsoTime(str(seg["timeFrame"]["startTime"]), b.start) || !parseIsoTime(str(seg["timeFrame"]["endTime"]), b.end) ||
        b.end <= b.start)
      continue;
    const double mm = num(seg["qpf"]["quantity"]);
    b.rate = mm > 0 ? mm * 3600.0 / static_cast<double>(b.end - b.start) : 0;
    b.probability = seg["probability"] | 0;
    out.push_back(b);
  }
  std::sort(out.begin(), out.end(), [](const RainBlock& x, const RainBlock& y) { return x.start < y.start; });
  int64_t t = 0;
  if (parseIsoTime(str(a["overallPredictionTimeframe"]["startTime"]), t)) updated = t;
  if (parseIsoTime(str(doc["last_updated"]), t)) updated = std::max(updated, t);
  return !out.empty();
}

double rainHeight(double rate) {
  if (!(rate > 0)) return 0;
  return std::min(1.0, std::log1p(rate * 4) / std::log1p(kRainTop * 4));
}

bool rainFresh(const std::vector<RainBlock>& blocks, int64_t updated, int64_t now) {
  return !blocks.empty() && now > 0 && now - updated < kRainStaleSec && blocks.back().end > now;
}

namespace {
bool wet(const RainBlock& b) { return b.rate >= 0.05; }
}  // namespace

std::vector<float> rainCurve(const std::vector<RainBlock>& blocks, int64_t from, int64_t to, int width) {
  std::vector<float> out(static_cast<size_t>(std::max(0, width)), 0.f);
  if (width <= 0 || to <= from) return out;
  std::vector<const RainBlock*> in;
  bool anyWet = false;
  for (const RainBlock& b : blocks)
    if (b.end > from && b.start < to) {
      in.push_back(&b);
      anyWet = anyWet || wet(b);
    }
  if (!anyWet) return out;
  // Points (time, height): the base at the start, the middle of each block, the base at the end (as the Thuis card).
  std::vector<double> px, py;
  px.push_back(static_cast<double>(std::max(from, in.front()->start)));
  py.push_back(0);
  for (const RainBlock* b : in) {
    px.push_back(std::min<double>(to, std::max<double>(from, (b->start + b->end) / 2.0)));
    py.push_back(rainHeight(b->rate));
  }
  px.push_back(static_cast<double>(std::min(to, in.back()->end)));
  py.push_back(0);
  const size_t n = px.size();
  // Fritsch-Carlson tangents: never under the base, never over a peak.
  std::vector<double> d(n - 1), m(n);
  for (size_t i = 0; i + 1 < n; ++i) {
    const double dx = px[i + 1] - px[i];
    d[i] = dx == 0 ? 0 : (py[i + 1] - py[i]) / dx;
  }
  for (size_t i = 0; i < n; ++i)
    m[i] = i == 0 ? d[0] : i == n - 1 ? d[n - 2] : (d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2);
  for (size_t i = 0; i + 1 < n; ++i) {
    if (d[i] == 0) {
      m[i] = m[i + 1] = 0;
      continue;
    }
    const double a = m[i] / d[i], b = m[i + 1] / d[i], s2 = a * a + b * b;
    if (s2 > 9) {
      const double t = 3 / std::sqrt(s2);
      m[i] = t * a * d[i];
      m[i + 1] = t * b * d[i];
    }
  }
  size_t seg = 0;
  for (int col = 0; col < width; ++col) {
    const double t = from + (col + 0.5) * static_cast<double>(to - from) / width;
    if (t < px.front() || t > px.back()) continue;
    while (seg + 2 < n && t > px[seg + 1]) ++seg;
    const double h = px[seg + 1] - px[seg];
    if (h <= 0) continue;
    const double u = (t - px[seg]) / h, u2 = u * u, u3 = u2 * u;
    const double v = (2 * u3 - 3 * u2 + 1) * py[seg] + (u3 - 2 * u2 + u) * h * m[seg] + (-2 * u3 + 3 * u2) * py[seg + 1] +
                     (u3 - u2) * h * m[seg + 1];
    out[static_cast<size_t>(col)] = static_cast<float>(std::clamp(v, 0.0, 1.0));
  }
  return out;
}

std::string clockText(int64_t t) {
  struct tm tmv;
  if (!localTime(t, tmv)) return "";
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return buf;
}

std::string rainHeadline(const std::vector<RainBlock>& blocks, int64_t now, int hours) {
  const int64_t until = now + hours * 3600;
  const std::string window = hours == 1 ? std::string("for the next hour") : "for the next " + std::to_string(hours) + " hours";
  const RainBlock* current = nullptr;
  for (const RainBlock& b : blocks)
    if (b.start <= now && now < b.end) current = &b;
  if (current && wet(*current)) {
    for (const RainBlock& b : blocks)
      if (b.start > now && b.start < until && !wet(b)) return "Rain until " + clockText(b.start);
    return "Rain " + window;
  }
  for (const RainBlock& b : blocks)
    if (b.end > now && b.start < until && wet(b)) return "Rain from " + clockText(std::max(b.start, now));
  return "Dry " + window;
}

double rainNow(const std::vector<RainBlock>& blocks, int64_t now) {
  for (const RainBlock& b : blocks)
    if (b.start <= now && now < b.end) return b.rate;
  return 0;
}

double rainPeak(const std::vector<RainBlock>& blocks, int64_t now, int hours) {
  double peak = 0;
  for (const RainBlock& b : blocks)
    if (b.end > now && b.start < now + hours * 3600) peak = std::max(peak, b.rate);
  return peak;
}

std::string rateText(double rate) {
  if (rate <= 0) return "0 mm/h";
  if (rate < 0.1) return "<0.1 mm/h";
  char buf[24];
  if (rate < 10) std::snprintf(buf, sizeof buf, "%.1f mm/h", rate);
  else std::snprintf(buf, sizeof buf, "%.0f mm/h", rate);
  return buf;
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

int localDay(int64_t t) {
  struct tm tmv;
  if (!localTime(t, tmv)) return static_cast<int>(t / 86400);
  return static_cast<int>(daysFromCivil(tmv.tm_year + 1900, static_cast<unsigned>(tmv.tm_mon + 1), static_cast<unsigned>(tmv.tm_mday)));
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
