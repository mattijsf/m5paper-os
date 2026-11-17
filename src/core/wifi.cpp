#include "core/wifi.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>

#include "core/fs.h"

namespace pos {

bool WifiStore::load() {
  networks_.clear();
  last_.clear();
  memory_.clear();
  std::string text;
  if (!fs::readFile(path_, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text) || !doc.is<JsonObject>()) return false;
  for (JsonVariantConst n : doc["networks"].as<JsonArrayConst>()) {
    if (!n["ssid"].is<const char*>() || !n["ssid"].as<const char*>()[0]) continue;
    WifiCred c;
    c.ssid = n["ssid"].as<const char*>();
    if (n["password"].is<const char*>()) c.password = n["password"].as<const char*>();
    networks_.push_back(c);
  }
  if (doc["last"].is<const char*>()) last_ = doc["last"].as<const char*>();
  for (JsonPairConst kv : doc["memory"].as<JsonObjectConst>()) {
    WifiMemory m;
    if (kv.value()["bssid"].is<const char*>()) m.bssid = kv.value()["bssid"].as<const char*>();
    m.channel = kv.value()["channel"] | 0;
    memory_[kv.key().c_str()] = m;
  }
  return true;
}

bool WifiStore::save() const {
  JsonDocument doc;
  JsonArray arr = doc["networks"].to<JsonArray>();
  for (const auto& c : networks_) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = c.ssid;
    o["password"] = c.password;
  }
  doc["last"] = last_;
  JsonObject mem = doc["memory"].to<JsonObject>();
  for (const auto& kv : memory_) {
    mem[kv.first]["bssid"] = kv.second.bssid;
    mem[kv.first]["channel"] = kv.second.channel;
  }
  std::string out;
  serializeJson(doc, out);
  return fs::writeFile(path_, out);
}

void WifiStore::add(const std::string& ssid, const std::string& password) {
  for (auto& c : networks_)
    if (c.ssid == ssid) {
      c.password = password;
      return;
    }
  WifiCred c;
  c.ssid = ssid;
  c.password = password;
  networks_.push_back(c);
}

bool WifiStore::forget(const std::string& ssid) {
  const size_t before = networks_.size();
  networks_.erase(std::remove_if(networks_.begin(), networks_.end(), [&](const WifiCred& c) { return c.ssid == ssid; }),
                  networks_.end());
  memory_.erase(ssid);
  if (last_ == ssid) last_.clear();
  return networks_.size() != before;
}

void WifiStore::remember(const std::string& ssid, const std::string& bssid, int channel) {
  last_ = ssid;
  memory_[ssid] = WifiMemory{bssid, channel};
}

std::optional<WifiMemory> WifiStore::memoryOf(const std::string& ssid) const {
  auto it = memory_.find(ssid);
  if (it == memory_.end() || it->second.bssid.empty()) return std::nullopt;
  return it->second;
}

std::vector<WifiCred> knownNetworks(const Secrets& card, const WifiStore& device) {
  std::vector<WifiCred> out = card.wifiNetworks();
  for (const auto& d : device.networks()) {
    bool dup = false;
    for (const auto& c : out)
      if (c.ssid == d.ssid) dup = true;
    if (!dup) out.push_back(d);
  }
  return out;
}

std::optional<WifiCred> fastCandidate(const std::vector<WifiCred>& known, const std::string& last) {
  if (last.empty()) return std::nullopt;
  for (const auto& c : known)
    if (c.ssid == last) return c;
  return std::nullopt;
}

std::optional<Pick> strongestKnown(const std::vector<WifiCred>& known, const std::vector<hal::WifiNetwork>& scan) {
  std::optional<Pick> best;
  for (const auto& ap : scan) {
    for (const auto& c : known) {
      if (c.ssid != ap.ssid) continue;
      if (c.open() == ap.secure) continue;  // an open network must be open on air, a secured one secured
      if (!best || ap.rssi > best->ap.rssi) best = Pick{c, ap};
    }
  }
  return best;
}

std::string scanSummary(const std::vector<hal::WifiNetwork>& scan, size_t maxEntries) {
  std::vector<hal::WifiNetwork> sorted = scan;
  std::sort(sorted.begin(), sorted.end(), [](const hal::WifiNetwork& a, const hal::WifiNetwork& b) { return a.rssi > b.rssi; });
  std::string out;
  for (size_t i = 0; i < sorted.size() && i < maxEntries; ++i) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s%s %d dBm ch%d", i ? ", " : "", sorted[i].ssid.empty() ? "(hidden)" : sorted[i].ssid.c_str(),
                  sorted[i].rssi, sorted[i].channel);
    out += buf;
  }
  if (sorted.size() > maxEntries) out += ", +" + std::to_string(sorted.size() - maxEntries) + " more";
  return out.empty() ? "no networks found" : out;
}

int ScanList::merge(const std::vector<hal::WifiNetwork>& scan) {
  std::vector<hal::WifiNetwork> fresh;  // names not listed yet, the strongest access point of each
  for (const hal::WifiNetwork& ap : scan) {
    if (ap.ssid.empty()) continue;
    auto same = [&ap](const hal::WifiNetwork& r) { return r.ssid == ap.ssid; };
    auto listed = std::find_if(rows_.begin(), rows_.end(), same);
    if (listed != rows_.end()) {
      if (ap.rssi > listed->rssi) *listed = ap;  // in place: the row stays where it is
      continue;
    }
    auto found = std::find_if(fresh.begin(), fresh.end(), same);
    if (found == fresh.end()) fresh.push_back(ap);
    else if (ap.rssi > found->rssi) *found = ap;
  }
  std::stable_sort(fresh.begin(), fresh.end(), [](const hal::WifiNetwork& a, const hal::WifiNetwork& b) { return a.rssi > b.rssi; });
  rows_.insert(rows_.end(), fresh.begin(), fresh.end());
  return static_cast<int>(fresh.size());
}

}  // namespace pos
