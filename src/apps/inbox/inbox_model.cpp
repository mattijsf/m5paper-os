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
  if (a["connected"].is<bool>()) out.linkDown = !a["connected"].as<bool>();
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

void cleanFeed(Feed& feed, bool mail) {
  CleanOptions plain, html;
  html.html = true;
  for (Msg& m : feed.items) {
    // A mail text is HTML when it says so; the feed's excerpt is plain more often than not.
    const bool looksHtml = mail && (m.text.find("<html") != std::string::npos || m.text.find("<body") != std::string::npos ||
                                    m.text.find("<p>") != std::string::npos || m.text.find("<div") != std::string::npos || m.text.find("<br") != std::string::npos);
    m.text = cleanText(m.text, looksHtml ? html : plain);
    m.from = cleanText(m.from, plain);
    m.chat = cleanText(m.chat, plain);
    m.subject = cleanText(m.subject, plain);
    for (char& c : m.from) if (c == '\n') c = ' ';
    for (char& c : m.subject) if (c == '\n') c = ' ';
  }
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
  char buf[16];
  if (t.tm_year == n.tm_year && t.tm_yday == n.tm_yday) {
    std::snprintf(buf, sizeof buf, "%02d:%02d", t.tm_hour, t.tm_min);
    return buf;
  }
  const int64_t days = (now - ts) / 86400;
  struct tm y;
  if (!localTime(now - 86400, y)) return "";
  if (t.tm_year == y.tm_year && t.tm_yday == y.tm_yday) return "Yesterday";
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  if (days < 6) return kDays[t.tm_wday];
  std::snprintf(buf, sizeof buf, "%d %s", t.tm_mday, kMonths[t.tm_mon]);
  return buf;
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

std::string clearBody(const std::string& feed, int64_t before) {
  JsonDocument doc;
  doc["feed"] = feed;
  if (before > 0) doc["before"] = before;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string fetchedText(const std::string& json) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return "";
  JsonVariantConst r = doc["service_response"];
  if (r["text"].is<const char*>()) return r["text"].as<const char*>();
  // imap.fetch may answer {"service_response": {"<entry>": {...}}} depending on the version: look one level down
  if (r.is<JsonObjectConst>())
    for (JsonPairConst kv : r.as<JsonObjectConst>())
      if (kv.value()["text"].is<const char*>()) return kv.value()["text"].as<const char*>();
  return "";
}

}  // namespace pos::inbox
