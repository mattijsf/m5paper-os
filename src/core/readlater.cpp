#include "core/readlater.h"

#include <ArduinoJson.h>

#include <algorithm>

#include "core/fs.h"
#include "core/store.h"
#include "core/strings.h"
#include "core/utf8.h"

namespace pos::readlater {

namespace {

constexpr const char* kPath = "/data/readlater.json";

bool isAddress(const std::string& word) {
  const std::string w = lowerAscii(word);
  const size_t scheme = startsWith(w, "https://") ? 8 : startsWith(w, "http://") ? 7 : 0;
  return scheme > 0 && w.size() > scheme && w[scheme] != '/' && w.size() <= kMaxUrlBytes;
}

}  // namespace

bool linkFromText(const std::string& text, std::string& url, std::string& hint) {
  const std::string t = trimmed(text);
  if (t.empty()) return false;
  const size_t cut = t.find_last_of(kBlanks);
  const std::string last = cut == std::string::npos ? t : t.substr(cut + 1);
  if (!isAddress(last)) return false;
  const std::string before = cut == std::string::npos ? std::string() : trimmed(t.substr(0, cut));
  // An address among the words before it: a note about links, not a link to read.
  const std::string lower = lowerAscii(before);
  if (lower.find("http://") != std::string::npos || lower.find("https://") != std::string::npos) return false;
  for (const char c : last)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return false;
  url = last;
  std::string words;
  for (const char c : before) words += (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) ? ' ' : c;
  hint = utf8::prefix(trimmed(words), kMaxHintChars);
  return true;
}

std::string hostOf(const std::string& url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return "";
  size_t from = scheme + 3;
  const size_t end = std::min(url.find_first_of("/?#", from), url.size());
  const size_t at = url.find('@', from);
  if (at != std::string::npos && at < end) from = at + 1;
  std::string host = lowerAscii(url.substr(from, end - from));
  const size_t port = host.rfind(':');
  if (port != std::string::npos && host.find(']') == std::string::npos) host.erase(port);
  if (startsWith(host, "www.")) host.erase(0, 4);
  return host;
}

std::string fileName(const std::string& title, const std::string& host) {
  std::string clean;
  bool blank = false;
  for (const char c : title) {
    const unsigned char u = static_cast<unsigned char>(c);
    const bool bad = u < 0x20 || u == 0x7F || std::string("\\/:*?\"<>|").find(c) != std::string::npos;
    if (bad || c == ' ') {
      blank = true;
      continue;
    }
    if (blank && !clean.empty()) clean += ' ';
    blank = false;
    clean += c;
  }
  for (size_t at = clean.find(" - "); at != std::string::npos; at = clean.find(" - ", at)) clean.replace(at, 3, " \xE2\x80\x93 ");
  clean = utf8::prefix(clean, kMaxTitleChars);
  while (!clean.empty() && (clean.back() == '.' || clean.back() == ' ')) clean.pop_back();  // FAT drops them: the name would differ
  if (!clean.empty() && clean[0] == '.') clean.erase(0, clean.find_first_not_of('.'));        // never a hidden file
  std::string site;
  for (const char c : host)
    if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-') site += c;
  if (clean.empty()) return (site.empty() ? std::string("Article") : site) + ".md";
  return site.empty() ? clean + ".md" : clean + " - " + site + ".md";
}

bool load(State& out) {
  out = State{};
  std::string text;
  if (!fs::readFile(kPath, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text) || !doc.is<JsonObjectConst>()) return false;
  for (JsonObjectConst o : doc["queue"].as<JsonArrayConst>()) {
    Link l;
    l.url = o["url"] | "";
    l.hint = o["hint"] | "";
    l.added = o["added"] | static_cast<int64_t>(0);
    l.tries = o["tries"] | 0;
    if (!l.url.empty() && static_cast<int>(out.queue.size()) < kMaxQueue) out.queue.push_back(l);
  }
  for (JsonVariantConst f : doc["fresh"].as<JsonArrayConst>())
    if (f.is<const char*>()) out.fresh.push_back(f.as<std::string>());
  return true;
}

bool save(const State& s) {
  JsonDocument doc;
  doc["v"] = 1;
  JsonArray queue = doc["queue"].to<JsonArray>();
  for (const Link& l : s.queue) {
    JsonObject o = queue.add<JsonObject>();
    o["url"] = l.url;
    if (!l.hint.empty()) o["hint"] = l.hint;
    o["added"] = l.added;
    if (l.tries > 0) o["tries"] = l.tries;
  }
  JsonArray fresh = doc["fresh"].to<JsonArray>();
  for (const std::string& f : s.fresh) fresh.add(f);
  std::string text;
  serializeJson(doc, text);
  setAppBadge(kBadgeApp, static_cast<int>(s.queue.size() + s.fresh.size()));
  return fs::writeFile(kPath, text);
}

bool add(State& s, const std::string& url, const std::string& hint, int64_t now) {
  for (const Link& l : s.queue)
    if (l.url == url) return false;
  while (static_cast<int>(s.queue.size()) >= kMaxQueue) s.queue.erase(s.queue.begin());
  s.queue.push_back(Link{url, hint, now, 0});
  return true;
}

int addShared(const std::vector<std::string>& texts, int64_t now) {
  State s;
  load(s);
  int added = 0;
  for (const std::string& text : texts) {
    std::string url, hint;
    if (linkFromText(text, url, hint) && add(s, url, hint, now)) ++added;
  }
  if (added > 0) save(s);
  return added;
}

}  // namespace pos::readlater
