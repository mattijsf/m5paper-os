// Wi-Fi bookkeeping that needs no radio: known networks (card + device), what worked last, and the choice of
// which network to join (docs/POWER.md "Wi-Fi policy", docs/apps/settings.md "Secrets and provisioning").
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/secrets.h"
#include "hal/net.h"

namespace pos {

// BSSID and channel of the access point a network last connected to (the ~1 s fast path).
struct WifiMemory {
  std::string bssid;
  int channel = 0;
};

// Networks added on the device and what worked last: /data/wifi.json (no secrets leave the device; never logged).
class WifiStore {
 public:
  explicit WifiStore(std::string path = "/data/wifi.json") : path_(std::move(path)) {}
  bool load();
  bool save() const;
  const std::vector<WifiCred>& networks() const { return networks_; }
  // Adds or replaces (same SSID) a network added on the device.
  void add(const std::string& ssid, const std::string& password);
  bool forget(const std::string& ssid);
  const std::string& last() const { return last_; }
  void remember(const std::string& ssid, const std::string& bssid, int channel);  // after a success: also sets last
  std::optional<WifiMemory> memoryOf(const std::string& ssid) const;

 private:
  std::string path_;
  std::vector<WifiCred> networks_;
  std::string last_;
  std::map<std::string, WifiMemory> memory_;
};

// Card networks first, then device networks; a device network with the SSID of a card network is dropped (the
// card wins).
std::vector<WifiCred> knownNetworks(const Secrets& card, const WifiStore& device);

// The network to try first, without scanning: the one that worked last, if it is still known.
std::optional<WifiCred> fastCandidate(const std::vector<WifiCred>& known, const std::string& last);

// The known network with the strongest signal in a scan (several access points of one SSID: the strongest
// counts). Open known networks match open scan entries only. None if no known network is in range.
struct Pick {
  WifiCred cred;
  hal::WifiNetwork ap;  // the scan entry chosen (BSSID, channel, RSSI)
};
std::optional<Pick> strongestKnown(const std::vector<WifiCred>& known, const std::vector<hal::WifiNetwork>& scan);

// The networks of "Join a network": one row per name, built up over several scans. One scan misses access points (a
// beacon not heard in the 0.3 s the radio listens on a channel), so the page scans a few times and adds what is new.
class ScanList {
 public:
  // Takes in the result of a scan and returns how many names are new. Hidden networks (no name) are left out; of
  // several access points with one name the strongest counts. A name already listed keeps its row (the list must not
  // move under a finger; its signal follows the strongest seen); new names come after it, strongest first.
  int merge(const std::vector<hal::WifiNetwork>& scan);
  // Forgets everything: the next merge is in order of signal again.
  void clear() { rows_.clear(); }
  const std::vector<hal::WifiNetwork>& rows() const { return rows_; }

 private:
  std::vector<hal::WifiNetwork> rows_;
};

// "HomeNet -48 dBm ch6, Other -70 dBm ch1": a log-safe summary of a scan (SSIDs, signal, channel; no passwords).
std::string scanSummary(const std::vector<hal::WifiNetwork>& scan, size_t maxEntries = 8);

}  // namespace pos
