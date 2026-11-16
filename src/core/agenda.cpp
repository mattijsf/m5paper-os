#include "core/agenda.h"

#include <algorithm>
#include <cstdio>

#include <ArduinoJson.h>

#include "core/fs.h"
#include "core/timefmt.h"

namespace pos::agenda {

namespace {

const char* kDir = "/data/cache/agenda";
const char* kPath = "/data/cache/agenda/agenda.json";

const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
const char* kMonths[] = {"January", "February", "March", "April", "May", "June",
                         "July", "August", "September", "October", "November", "December"};

std::string cut(const std::string& s, size_t chars) {
  size_t i = 0, n = 0;
  while (i < s.size() && n < chars) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    ++n;
  }
  return s.substr(0, std::min(i, s.size()));
}

std::string str(JsonVariantConst v, size_t chars) { return v.is<const char*>() ? cut(v.as<std::string>(), chars) : std::string(); }

std::string hhmm(int minutes) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", (minutes / 60) % 24, minutes % 60);
  return buf;
}

const std::string& titleOf(const Event& e) {
  static const std::string kNone = "(No title)";
  return e.title.empty() ? kNone : e.title;
}

// One event from its JSON object (the wire and the stored form are the same). False if a time is out of range.
bool readEvent(JsonObjectConst o, Event& e) {
  e = Event{};
  if (!o["s"].isNull()) {
    if (!o["s"].is<int>()) return false;
    e.start = o["s"].as<int>();
    if (e.start < 0 || e.start > 1439) return false;
  }
  if (!o["e"].isNull()) {
    if (!o["e"].is<int>()) return false;
    e.end = o["e"].as<int>();
    if (e.end < 0 || e.end > 1440 || (e.start >= 0 && e.end < e.start)) return false;
  }
  e.title = str(o["n"], kMaxTitleChars);
  e.location = str(o["l"], kMaxLocationChars);
  return true;
}

void readEvents(JsonArrayConst ev, Day& day) {
  for (JsonObjectConst o : ev) {
    if (day.events.size() >= static_cast<size_t>(kMaxEventsPerDay)) break;
    Event e;
    if (readEvent(o, e)) day.events.push_back(std::move(e));
  }
}

void writeDay(JsonArray days, const Day& day) {
  JsonObject o = days.add<JsonObject>();
  o["d"] = formatDate(day.date);
  JsonArray ev = o["ev"].to<JsonArray>();
  for (const Event& e : day.events) {
    JsonObject eo = ev.add<JsonObject>();
    if (e.start >= 0) eo["s"] = e.start;
    if (e.end >= 0) eo["e"] = e.end;
    eo["n"] = e.title;
    if (!e.location.empty()) eo["l"] = e.location;
  }
  if (day.more > 0) o["more"] = day.more;
}

}  // namespace

// Howard Hinnant's civil-date algorithms.
int daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int>(doe) - 719468;
}

void civilFromDays(int days, int& y, int& m, int& d) {
  days += 719468;
  const int era = (days >= 0 ? days : days - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(days - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  y = static_cast<int>(yoe) + era * 400 + (m <= 2);
}

int weekday(int days) { return ((days % 7) + 11) % 7; }  // 1970-01-01 was a Thursday

bool parseDate(const std::string& text, int& days) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-') return false;
  for (size_t i = 0; i < text.size(); ++i)
    if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9')) return false;
  const int y = std::atoi(text.substr(0, 4).c_str()), m = std::atoi(text.substr(5, 2).c_str()), d = std::atoi(text.substr(8, 2).c_str());
  if (y < 1970 || m < 1 || m > 12 || d < 1 || d > 31) return false;
  days = daysFromCivil(y, m, d);
  int y2, m2, d2;
  civilFromDays(days, y2, m2, d2);
  return y2 == y && m2 == m && d2 == d;  // no 31 February
}

std::string formatDate(int days) {
  int y, m, d;
  civilFromDays(days, y, m, d);
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
  return buf;
}

bool load(Agenda& out) {
  out = Agenda{};
  std::string text;
  if (!fs::readFile(kPath, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  out.received = doc["received"].as<bool>();
  out.updated = doc["updated"].as<int64_t>();
  for (JsonObjectConst o : doc["days"].as<JsonArrayConst>()) {
    Day day;
    if (!parseDate(o["d"].as<std::string>(), day.date)) continue;
    readEvents(o["ev"].as<JsonArrayConst>(), day);
    day.more = std::max(0, o["more"].as<int>());
    if (!day.events.empty() || day.more > 0) out.days.push_back(std::move(day));
  }
  std::sort(out.days.begin(), out.days.end(), [](const Day& a, const Day& b) { return a.date < b.date; });
  return true;
}

bool save(const Agenda& a) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["received"] = a.received;
  doc["updated"] = a.updated;
  JsonArray days = doc["days"].to<JsonArray>();
  for (const Day& day : a.days) writeDay(days, day);
  std::string text;
  serializeJson(doc, text);
  fs::mkdirs(kDir);
  return fs::writeFile(kPath, text);
}

void forget() { fs::removeAll(kPath); }

bool apply(Agenda& a, const std::string& plain, int64_t now) {
  JsonDocument doc;
  if (deserializeJson(doc, plain) || !doc.is<JsonObjectConst>() || doc["t"] != "agenda") return false;
  int from = 0, to = 0;
  if (!doc["from"].is<const char*>() || !doc["to"].is<const char*>()) return false;
  if (!parseDate(doc["from"].as<std::string>(), from) || !parseDate(doc["to"].as<std::string>(), to)) return false;
  if (to < from || to - from >= kMaxRunDays) return false;
  std::vector<Day> fresh;
  for (JsonObjectConst o : doc["days"].as<JsonArrayConst>()) {
    if (!o["d"].is<int>()) continue;
    const int d = o["d"].as<int>();
    if (d < 0 || d > to - from) continue;
    Day day;
    day.date = from + d;
    if (std::any_of(fresh.begin(), fresh.end(), [&](const Day& x) { return x.date == day.date; })) continue;  // each day once
    readEvents(o["ev"].as<JsonArrayConst>(), day);
    day.more = std::max(0, o["more"].as<int>());
    if (!day.events.empty() || day.more > 0) fresh.push_back(std::move(day));
  }
  a.days.erase(std::remove_if(a.days.begin(), a.days.end(), [&](const Day& x) { return x.date >= from && x.date <= to; }), a.days.end());
  for (Day& day : fresh) a.days.push_back(std::move(day));
  std::sort(a.days.begin(), a.days.end(), [](const Day& x, const Day& y) { return x.date < y.date; });
  a.received = true;
  a.updated = now > 0 ? now : 0;
  return true;
}

bool expire(Agenda& a, int today) {
  const size_t before = a.days.size();
  a.days.erase(std::remove_if(a.days.begin(), a.days.end(), [&](const Day& x) { return x.date < today; }), a.days.end());
  return a.days.size() != before;
}

bool localNow(int64_t now, int& today, int& minute) {
  struct tm tmv;
  if (!localTime(now, tmv)) return false;
  today = daysFromCivil(tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
  minute = tmv.tm_hour * 60 + tmv.tm_min;
  return true;
}

std::string timeText(const Event& e) {
  if (e.allDay()) return "All day";
  if (e.start < 0) return "Until " + hhmm(e.end);
  if (e.end < 0) return "From " + hhmm(e.start);
  if (e.end == e.start) return hhmm(e.start);
  return hhmm(e.start) + " - " + hhmm(e.end);
}

std::string dayTitle(int date, int today) {
  if (date == today) return "Today";
  if (date == today + 1) return "Tomorrow";
  int y, m, d;
  civilFromDays(date, y, m, d);
  return std::string(kDays[weekday(date)]) + " " + std::to_string(d) + " " + kMonths[m - 1];
}

std::string nextLine(const Agenda& a, int today, int minute) {
  for (const Day& day : a.days) {
    if (day.date < today || day.events.empty()) continue;
    if (day.date == today) {
      const Event *ongoing = nullptr, *upcoming = nullptr, *allDay = nullptr;
      for (const Event& e : day.events) {
        if (e.allDay()) {
          if (!allDay) allDay = &e;
        } else if (e.start > minute) {
          if (!upcoming) upcoming = &e;
        } else if (e.end < 0 || e.end > minute) {  // began earlier (today or before) and is not over
          if (!ongoing) ongoing = &e;
        }
      }
      if (ongoing) return "Now: " + titleOf(*ongoing) + (ongoing->end >= 0 ? " until " + hhmm(ongoing->end) : std::string());
      if (upcoming) return hhmm(upcoming->start) + " " + titleOf(*upcoming);
      if (allDay) return "Today: " + titleOf(*allDay);
      continue;  // everything of today is over
    }
    const Event& e = day.events.front();
    std::string label;
    if (day.date == today + 1) {
      label = "Tomorrow";
    } else {
      int y, m, d;
      civilFromDays(day.date, y, m, d);
      label = std::string(kDays[weekday(day.date)]).substr(0, 3) + " " + std::to_string(d) + " " + std::string(kMonths[m - 1]).substr(0, 3);
    }
    return e.start < 0 ? label + ": " + titleOf(e) : label + " " + hhmm(e.start) + " " + titleOf(e);
  }
  return "";
}

std::string nextLine(const Agenda& a, int64_t now) {
  int today = 0, minute = 0;
  if (!localNow(now, today, minute)) return "";
  return nextLine(a, today, minute);
}

}  // namespace pos::agenda
