#include "core/secrets.h"

#include "core/fs.h"
#include "core/strings.h"

namespace pos {

bool Secrets::load() {
  items_.clear();
  found_ = false;
  std::string text;
  if (!fs::readFile(path_, text)) return false;
  found_ = true;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = trimmed(text.substr(pos, end - pos));
    pos = end + 1;
    if (line.empty() || line[0] == '#') continue;
    size_t eq = line.find('=');
    if (eq == std::string::npos || eq == 0) continue;
    std::string key = trimmed(line.substr(0, eq));
    if (key.empty()) continue;
    items_.emplace_back(key, trimmed(line.substr(eq + 1)));
  }
  return true;
}

bool Secrets::present(const std::string& key) const {
  for (auto& kv : items_)
    if (kv.first == key && !kv.second.empty()) return true;
  return false;
}

std::string Secrets::get(const std::string& key) const {
  for (auto& kv : items_)
    if (kv.first == key) return kv.second;
  return "";
}

std::vector<std::string> Secrets::keys() const {
  std::vector<std::string> out;
  for (auto& kv : items_) out.push_back(kv.first);
  return out;
}

}  // namespace pos

namespace pos {

std::vector<WifiCred> Secrets::wifiNetworks() const {
  std::vector<WifiCred> out;
  for (int i = 1; i <= 8; ++i) {
    const std::string suffix = i == 1 ? "" : "_" + std::to_string(i);
    const std::string ssid = get("WIFI_SSID" + suffix);
    if (ssid.empty()) continue;
    WifiCred c;
    c.ssid = ssid;
    c.password = get("WIFI_PASSWORD" + suffix);
    c.fromCard = true;
    out.push_back(c);
  }
  return out;
}

}  // namespace pos
