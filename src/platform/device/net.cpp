#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <ctime>

#include "core/log.h"
#include "platform/device/device.h"

namespace pos::device {

namespace {

constexpr size_t kBodyCap = 512 * 1024;

bool parseBssid(const std::string& s, uint8_t out[6]) {
  unsigned b[6];
  if (std::sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) return false;
  for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(b[i]);
  return true;
}

// One kept-alive connection: the TLS handshake (about 1.5 s, 45 KB) is paid once per refresh.
NetworkClientSecure gSecure;
NetworkClient gPlain;
HTTPClient gHttp;

}  // namespace

bool Net::wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs, const std::string& bssid, int channel) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  uint8_t bs[6];
  const bool fast = !bssid.empty() && channel > 0 && parseBssid(bssid, bs);
  if (fast) WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str(), channel, bs, true);
  else WiFi.begin(ssid.c_str(), pass.empty() ? nullptr : pass.c_str());
  const uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(50);
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(true, false);
  return false;
}

void Net::wifiOff() {
  gHttp.end();
  gSecure.stop();
  gPlain.stop();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

bool Net::wifiConnected() { return WiFi.status() == WL_CONNECTED; }

int Net::rssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

hal::WifiLink Net::link() {
  hal::WifiLink l;
  if (WiFi.status() != WL_CONNECTED) return l;
  l.connected = true;
  l.ssid = WiFi.SSID().c_str();
  l.bssid = WiFi.BSSIDstr().c_str();
  l.ip = WiFi.localIP().toString().c_str();
  l.channel = WiFi.channel();
  l.rssi = WiFi.RSSI();
  return l;
}

std::vector<hal::WifiNetwork> Net::scan() {
  std::vector<hal::WifiNetwork> out;
  WiFi.mode(WIFI_STA);
  const int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    hal::WifiNetwork w;
    w.ssid = WiFi.SSID(i).c_str();
    w.rssi = WiFi.RSSI(i);
    w.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    w.channel = WiFi.channel(i);
    w.bssid = WiFi.BSSIDstr(i).c_str();
    out.push_back(w);
  }
  WiFi.scanDelete();
  return out;
}

hal::HttpResponse Net::request(const hal::HttpRequest& req) {
  hal::HttpResponse r;
  if (WiFi.status() != WL_CONNECTED) {
    r.error = "not connected";
    return r;
  }
  const bool https = req.url.rfind("https://", 0) == 0;
  gHttp.setReuse(true);
  gHttp.setTimeout(req.timeoutMs);
  gHttp.setConnectTimeout(req.timeoutMs);
  bool begun;
  if (https) {
    gSecure.useBuiltinCACertBundle();  // certificates are verified: the token travels over TLS
    begun = gHttp.begin(gSecure, req.url.c_str());
  } else {
    begun = gHttp.begin(gPlain, req.url.c_str());
  }
  if (!begun) {
    r.error = "bad URL";
    return r;
  }
  for (const auto& h : req.headers) gHttp.addHeader(h.first.c_str(), h.second.c_str());
  const int code = req.body.empty() ? gHttp.sendRequest(req.method.c_str())
                                    : gHttp.sendRequest(req.method.c_str(), reinterpret_cast<uint8_t*>(const_cast<char*>(req.body.data())), req.body.size());
  if (code < 0) {
    r.error = HTTPClient::errorToString(code).c_str();
    gHttp.end();
    return r;
  }
  r.status = code;
  String body = gHttp.getString();
  if (body.length() > kBodyCap) {
    r.error = "response too large";
    r.body.assign(body.c_str(), kBodyCap);
  } else {
    r.body.assign(body.c_str(), body.length());
  }
  gHttp.end();  // keeps the connection (setReuse) for the next request to the same host
  return r;
}

int64_t Net::syncTime(uint32_t timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    const time_t now = time(nullptr);
    if (now > 1700000000) return now;
    delay(100);
  }
  return 0;
}

}  // namespace pos::device
