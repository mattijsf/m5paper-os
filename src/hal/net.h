// HAL: Wi-Fi and HTTP. Device: WiFi + HTTPClient. Simulator: fake networks and fixtures (or libcurl).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pos::hal {

struct WifiNetwork {
  std::string ssid;
  int rssi = 0;
  bool secure = true;
  int channel = 0;
  std::string bssid;  // "AA:BB:CC:DD:EE:FF", "" if unknown
};

// The current connection.
struct WifiLink {
  bool connected = false;
  std::string ssid, bssid, ip;
  int channel = 0, rssi = 0;
};

struct HttpRequest {
  std::string method = "GET", url, body;
  std::vector<std::pair<std::string, std::string>> headers;  // may hold an Authorization header: never log it
  uint32_t timeoutMs = 10000;
};

struct HttpResponse {
  int status = 0;  // 0 = transport error, see `error`
  std::string body;
  std::string error;
};

class Net {
 public:
  virtual ~Net() = default;
  // Joins a network (blocking, worker thread only). With `bssid` and `channel` the radio skips the scan (the ~1 s
  // fast path). Empty password = open network.
  virtual bool wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs, const std::string& bssid = "",
                           int channel = 0) = 0;
  virtual void wifiOff() = 0;
  virtual bool wifiConnected() = 0;
  virtual int rssi() = 0;
  virtual WifiLink link() = 0;
  virtual std::vector<WifiNetwork> scan() = 0;  // blocking, a few seconds
  // Blocking: call from a worker thread only (ctx.http, docs/ARCHITECTURE.md "Runtime model").
  virtual HttpResponse request(const HttpRequest&) = 0;
  // SNTP: returns unix seconds, 0 if it did not work. Needs a connection.
  virtual int64_t syncTime(uint32_t /*timeoutMs*/) { return 0; }
};

}  // namespace pos::hal
