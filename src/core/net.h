// Net service: joins Wi-Fi on demand, keeps it up for a while after the last request ("linger"), switches it off,
// and syncs the clock after a connect (docs/POWER.md "Wi-Fi policy"). Apps never touch the radio: they use
// ctx.http(), which calls perform() on the worker thread.
//
// Connecting never loops: one fast attempt (the network that worked last, with its stored BSSID and channel), then
// one scan-and-connect attempt (the known network with the strongest signal), then the failure is reported.
#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/secrets.h"
#include "core/wifi.h"
#include "core/worker.h"
#include "hal/platform.h"

namespace pos {

class NetService {
 public:
  NetService(hal::Platform& platform, Config& config, const Secrets& secrets)
      : p_(platform), config_(config), secrets_(secrets) {}

  struct Result {
    bool ok = false;
    bool fast = false;       // joined through the stored BSSID / channel without a scan
    std::string ssid;        // the network joined (or tried last)
    std::string error;       // "No Wi-Fi networks configured", "No known network in range", ...
    std::string scan;        // log-safe scan summary when a scan was made (SSIDs, signal, channel)
    uint32_t ms = 0;         // time the attempt took
  };

  // Loads /data/wifi.json (networks added on the device, what worked last). After the storage is mounted.
  void load() { store_.load(); }

  // --- worker thread (blocking) ---
  // Makes sure Wi-Fi is connected; one fast attempt, one scan attempt, no retries.
  Result ensureConnected();
  // ensureConnected() + the request. A failed connect gives status 0 and error "No Wi-Fi: ...".
  hal::HttpResponse perform(const hal::HttpRequest& req);
  // Joins a specific network (the Wi-Fi screen); on success it is remembered and, if `save`, stored on the device.
  Result connectTo(const std::string& ssid, const std::string& password, bool save);
  std::vector<hal::WifiNetwork> scan();

  // --- UI thread ---
  // Call every loop pass: switches Wi-Fi off after the linger time, applies a time sync, reports state changes.
  void tick();
  // Milliseconds until tick() has something to do (the linger end), kNoDeadline if none.
  uint32_t msToNextDeadline();
  // Cached: UI code must not call the radio HAL while the worker is connecting or running TLS (the Wi-Fi stack
  // locks are held then and the UI thread stalled for seconds: measured 8 s on the device).
  bool online() const { return connected_; }
  hal::WifiLink link();
  void disconnect();
  Result lastResult();
  // The worker that runs the clock sync after a connect (set by the Os).
  void setWorker(Worker* w) { worker_ = w; }
  // Wi-Fi or the clock changed (status bar): set by the Shell.
  std::function<void()> onChange;
  // Who set onChange: a Shell that is replaced by a new one (set screen, restart) must not clear the new one's callback when it dies.
  const void* onChangeOwner = nullptr;

  WifiStore& store() { return store_; }
  std::vector<WifiCred> known() const { return knownNetworks(secrets_, store_); }
  // The Home Assistant base URL: HA_URL from the card, else config.json (never log the card value's source file).
  std::string haBaseUrl() const;

 private:
  Result connectLocked();
  void syncClock();
  void succeeded(Result& r, bool fast);
  void markActive();

  hal::Platform& p_;
  Config& config_;
  const Secrets& secrets_;
  WifiStore store_;
  std::mutex mu_;
  std::atomic<int> inFlight_{0};
  std::atomic<uint32_t> lastActiveMs_{0};
  std::atomic<int64_t> pendingTime_{0};
  int64_t lastSyncUnix_ = 0;
  bool wasOnline_ = false;
  std::atomic<bool> needSync_{false};
  std::atomic<bool> connected_{false};
  std::mutex linkMu_;
  hal::WifiLink linkCache_;  // for the UI thread: refreshed by the worker after connects and requests
  void cacheLink();
  Worker* worker_ = nullptr;
  Result last_;
};

}  // namespace pos
