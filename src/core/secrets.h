// Secrets from /sd/paperos_config (docs/apps/settings.md "Secrets and provisioning"): KEY=VALUE per
// line. Read-only: the device never writes, logs or shows the values. UI code may ask which keys exist
// (names and "present" only); only the feature that needs a secret calls get().
#pragma once

#include <string>
#include <vector>

namespace pos {

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

 private:
  std::string path_;
  std::vector<std::pair<std::string, std::string>> items_;
  bool found_ = false;
};

}  // namespace pos
