#include "apps/inbox/inbox_model.h"

#include <algorithm>
#include <ctime>
#include <map>

#include <ArduinoJson.h>

#include "core/text_clean.h"
#include "core/timefmt.h"

namespace pos::inbox {

namespace {
std::string str(JsonVariantConst v) {
  if (v.is<const char*>()) return v.as<const char*>();
  if (v.is<long long>()) return std::to_string(v.as<long long>());
  return "";
}
}  // namespace

bool parseFeed(const std::string& json, Feed& out) {
  out = Feed{};
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["attributes"].is<JsonObjectConst>()) return false;
  JsonObjectConst a = doc["attributes"].as<JsonObjectConst>();
  out.updated = str(a["updated"]);
  for (JsonObjectConst it : a["items"].as<JsonArrayConst>()) {
    Msg m;
    m.id = str(it["id"]);
    m.from = str(it["from"]);
    m.chat = str(it["chat"]);
    m.subject = str(it["subject"]);
    m.text = str(it["text"]);
    m.key = str(it["key"]);
    m.ts = it["ts"].is<long long>() ? it["ts"].as<long long>() : 0;
    if (m.text.empty() && m.subject.empty()) continue;
    out.items.push_back(std::move(m));
  }
  return true;
}

std::vector<Conversation> groupChats(const std::vector<Msg>& items, int64_t seen) {
  std::vector<Conversation> out;
  std::map<std::string, size_t> byKey;
  for (size_t i = 0; i < items.size(); ++i) {
    const Msg& m = items[i];
    size_t at;
    auto it = m.key.empty() ? byKey.end() : byKey.find(m.key);
    if (it == byKey.end()) {
      Conversation c;
      c.key = m.key;
      c.title = m.chat.empty() ? m.from : m.from + " - " + m.chat;
      c.lastText = m.text;
      c.ts = m.ts;
      out.push_back(std::move(c));
      at = out.size() - 1;
      if (!m.key.empty()) byKey[m.key] = at;
    } else {
      at = it->second;
    }
    Conversation& c = out[at];
    ++c.count;
    c.items.push_back(static_cast<int>(i));
    if (m.ts > c.ts) {  // the feed is newest first, but do not rely on it
      c.ts = m.ts;
      c.lastText = m.text;
      c.title = m.chat.empty() ? m.from : m.from + " - " + m.chat;
    }
    if (m.ts > seen) c.isNew = true;
  }
  std::stable_sort(out.begin(), out.end(), [](const Conversation& a, const Conversation& b) { return a.ts > b.ts; });
  return out;
}

std::string relativeTime(int64_t ts, int64_t now) {
  if (ts <= 0 || now <= 0 || ts > now + 24 * 3600) return "";
  struct tm t, n;
  if (!localTime(ts, t) || !localTime(now, n)) return "";
  if (t.tm_year == n.tm_year && t.tm_yday == n.tm_yday) return hhmm(t.tm_hour * 60 + t.tm_min);
  const int64_t days = (now - ts) / 86400;
  struct tm y;
  if (!localTime(now - 86400, y)) return "";
  if (t.tm_year == y.tm_year && t.tm_yday == y.tm_yday) return "Yesterday";
  if (days < 6) return weekdayShort(t.tm_wday);
  return std::to_string(t.tm_mday) + " " + monthShort(t.tm_mon);
}

int newCount(const std::vector<Msg>& items, int64_t seen) {
  int n = 0;
  for (const Msg& m : items)
    if (m.ts > seen) ++n;
  return n;
}

int64_t newestTs(const std::vector<Msg>& items) {
  int64_t t = 0;
  for (const Msg& m : items) t = std::max(t, m.ts);
  return t;
}

std::string firstLine(const std::string& text) {
  const size_t nl = text.find('\n');
  return nl == std::string::npos ? text : text.substr(0, nl);
}

std::vector<std::string> clearUpTo(companion::Notifs& notifs, bool mail, int64_t upTo) {
  std::vector<std::string> keys;
  auto gone = [&](const companion::Notif& n) {
    if ((n.app == "mail") != mail) return false;
    int64_t newest = n.msgs.empty() ? n.ts : 0;
    for (const companion::NotifMsg& m : n.msgs) newest = std::max(newest, m.ts > 0 ? m.ts : n.ts);  // as feedFromNotifs dates them
    if (newest > upTo) return false;
    keys.push_back(n.key);
    return true;
  };
  notifs.items.erase(std::remove_if(notifs.items.begin(), notifs.items.end(), gone), notifs.items.end());
  return keys;
}

Feed feedFromNotifs(const companion::Notifs& notifs, bool mail) {
  Feed f;
  for (const companion::Notif& n : notifs.items) {
    if ((n.app == "mail") != mail) continue;
    for (size_t i = 0; i < n.msgs.size(); ++i) {
      const companion::NotifMsg& nm = n.msgs[i];
      Msg m;
      m.id = n.key + "#" + std::to_string(i);
      m.from = nm.from;
      m.chat = n.chat;
      m.subject = nm.subject;
      m.text = nm.text;
      m.key = n.key;
      m.ts = nm.ts > 0 ? nm.ts : n.ts;
      m.canReply = n.reply && !mail;
      f.items.push_back(std::move(m));
    }
  }
  std::stable_sort(f.items.begin(), f.items.end(), [](const Msg& a, const Msg& b) { return a.ts > b.ts; });
  return f;
}

}  // namespace pos::inbox
