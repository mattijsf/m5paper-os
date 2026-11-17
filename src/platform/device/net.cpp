#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_sntp.h>

#include <ctime>

#include "core/timefmt.h"
#include "core/log.h"
#include "platform/device/device.h"

// ESP-IDF is built without SoftAP support (platformio.ini, D31), which also drops this function; Arduino's WiFi library still
// asks for the access point's network interface when Wi-Fi starts and copes with "none" (it checks for NULL before every use).
extern "C" esp_netif_t* esp_netif_create_default_wifi_ap(void) { return nullptr; }

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
// Arduino's HTTPClient reads a body of known length in a loop that only calls delay(0) (which never lets the lower-priority
// IDLE task run) around NetworkClient::readBytes(), and that returns at once with 0 bytes when the TLS client's read() says
// "no data yet" (-1). On the worker (core 0, priority 1) a slow server kept that loop spinning for > 5 s: IDLE0 never ran and
// the task watchdog rebooted the board (2026-11-16, Woordle's word list; backtrace through writeToStreamDataBlock). These
// clients sleep 1 ms whenever a read asked for data and got none, so the loop waits instead of spinning.
template <class Base>
class YieldingClient : public Base {
 public:
  int read() override {
    const int c = Base::read();
    if (c < 0) delay(1);
    return c;
  }
  int read(uint8_t* buf, size_t size) override {
    const int r = Base::read(buf, size);
    if (size > 0 && r <= 0) delay(1);
    return r;
  }
};
YieldingClient<NetworkClientSecure> gSecure;
YieldingClient<NetworkClient> gPlain;
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

void Net::setPowerSave(bool on) {
  if (WiFi.getMode() == WIFI_OFF) return;
  WiFi.setSleep(on);  // WIFI_PS_MIN_MODEM or WIFI_PS_NONE
  POS_LOG("net", "Wi-Fi power save %s", on ? "on" : "off");
}

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
  const uint32_t tStart = millis();
  uint32_t tDns = 0, tConnect = 0;
  bool reused = false;
  if (https) {
    // Connect by hand first so DNS and the TCP + TLS handshake can be timed separately (HTTPClient then reuses it).
    std::string host = req.url.substr(8);
    host = host.substr(0, host.find_first_of("/?"));
    int port = 443;
    const size_t colon = host.find(':');
    if (colon != std::string::npos) {
      port = atoi(host.c_str() + colon + 1);
      host = host.substr(0, colon);
    }
    if (gSecure.connected()) {
      reused = true;  // the kept-alive connection of the previous request
    } else {
      gSecure.useBuiltinCACertBundle();
      IPAddress ip;
      const uint32_t t0 = millis();
      if (!WiFi.hostByName(host.c_str(), ip)) {
        r.error = "DNS failed";
        return r;
      }
      tDns = millis() - t0;
      gSecure.setTimeout(req.timeoutMs / 1000 ? req.timeoutMs / 1000 : 1);
      const uint32_t t1 = millis();
      if (!gSecure.connect(ip, port, host.c_str(), nullptr, nullptr, nullptr)) {
        r.error = "TLS connect failed";
        return r;
      }
      tConnect = millis() - t1;
    }
  }
  const uint32_t tReq0 = millis();
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
  const uint32_t tHeaders = millis() - tReq0;
  const uint32_t tBody0 = millis();
  String body = gHttp.getString();
  POS_LOG("net", "https phases: dns=%ums tls+tcp=%ums request+headers=%ums body=%ums total=%ums cpu=%uMHz reused=%d heap=%uKB", static_cast<unsigned>(tDns),
          static_cast<unsigned>(tConnect), static_cast<unsigned>(tHeaders), static_cast<unsigned>(millis() - tBody0),
          static_cast<unsigned>(millis() - tStart), static_cast<unsigned>(getCpuFrequencyMhz()), reused ? 1 : 0,
          static_cast<unsigned>(ESP.getFreeHeap() / 1024));
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
  // Arduino's configTime() sets TZ to UTC: keep the zone the Os applied from config.json (net.timezone).
  const char* old = getenv("TZ");
  const std::string savedTz = old ? old : "";
  struct Restore {
    const std::string& tz;
    ~Restore() {
      if (!tz.empty()) {
        setenv("TZ", tz.c_str(), 1);
        tzset();
      }
    }
  } restore{savedTz};
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  const uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    const time_t now = time(nullptr);
    if (clockIsSet(now)) return now;
    delay(100);
  }
  return 0;
}

}  // namespace pos::device
