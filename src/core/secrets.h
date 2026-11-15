// Secrets from /sd/paperos_config (docs/apps/settings.md "Secrets and provisioning"): KEY=VALUE per
// line. Read-only: the device never writes, logs or shows the values. UI code may ask which keys exist
// (names and "present" only); only the feature that needs a secret calls get().
#pragma once

#include <string>
#include <vector>

namespace pos {

// One Wi-Fi network: from the card (`WIFI_SSID`, `WIFI_PASSWORD`, `WIFI_SSID_2` ... `_8`) or added on the device.
struct WifiCred {
  std::string ssid, password;  // empty password = open network
  bool open() const { return password.empty(); }
  bool fromCard = false;
};

class Secrets {
 public:
  explicit Secrets(std::string path = "/sd/paperos_config") : path_(std::move(path)) {}
  // Parses the file: split at the first '=', trim spaces and CR, ignore blank lines, '#' lines and lines
  // without a key. No file = nothing configured. Returns true if the file was read.
  bool load();
  bool present(const std::string& key) const;
  // The value, "" if absent. Never log or display the result.
  std::string get(const std::string& key) const;
  // Key names in file order (no values).
  std::vector<std::string> keys() const;
  bool fileFound() const { return found_; }
  // Value length without exposing the value (for status output).
  size_t valueLength(const std::string& key) const { return get(key).size(); }
  // Wi-Fi networks of the card in order: WIFI_SSID + WIFI_PASSWORD, then WIFI_SSID_2 ... WIFI_SSID_8. The SSID is
  // everything after the first '=' of its line, trimmed (it may contain '/', '.', spaces and '='). A network
  // without a password key (or with an empty one) is open. Passwords are never logged.
  std::vector<WifiCred> wifiNetworks() const;

 private:
  std::string path_;
  std::vector<std::pair<std::string, std::string>> items_;
  bool found_ = false;
};

}  // namespace pos
