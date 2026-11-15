// HAL: Wi-Fi and HTTP. Device: WiFi + HTTPClient. Simulator: host network or canned fixtures.
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
};

struct HttpRequest {
  std::string method = "GET", url, body;
  std::vector<std::pair<std::string, std::string>> headers;
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
  virtual bool wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs) = 0;
  virtual void wifiOff() = 0;
  virtual bool wifiConnected() = 0;
  virtual int rssi() = 0;
  virtual std::vector<WifiNetwork> scan() = 0;
  // Blocking: call from a worker thread only (ctx.http, docs/ARCHITECTURE.md "Runtime model").
  virtual HttpResponse request(const HttpRequest&) = 0;
};

}  // namespace pos::hal
