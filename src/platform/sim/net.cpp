// Simulator Net: a small fake Wi-Fi world (so the Wi-Fi logic runs without a radio) and HTTP from fixtures
// (tests/fixtures/http) or, with --fixtures none, from the real network through libcurl.
#include <curl/curl.h>

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include "core/http_fixture.h"
#include "core/log.h"
#include "platform/sim/sim.h"

namespace pos::sim {

namespace {

const std::vector<hal::WifiNetwork>& world() {
  static const std::vector<hal::WifiNetwork> w = {
      {"HomeNet", -48, true, 6, "AA:BB:CC:00:00:01"},
      {"HomeNet", -61, true, 11, "AA:BB:CC:00:00:02"},
      {"Neighbour", -72, true, 1, "DE:AD:BE:EF:00:01"},
      {"CafeFree", -66, false, 9, "CA:FE:00:00:00:01"},
  };
  return w;
}

size_t writeBody(char* data, size_t sz, size_t n, void* user) {
  auto* out = static_cast<std::string*>(user);
  if (out->size() + sz * n > 512 * 1024) return 0;  // body cap
  out->append(data, sz * n);
  return sz * n;
}

}  // namespace

bool Net::wifiConnect(const std::string& ssid, const std::string& pass, uint32_t, const std::string& bssid, int) {
  link_ = hal::WifiLink{};
  if (!wifiAvailable_) return false;
  for (const auto& ap : world()) {
    if (ap.ssid != ssid) continue;
    if (!bssid.empty() && ap.bssid != bssid) continue;
    if (ap.secure && pass.empty()) return false;
    link_.connected = true;
    link_.ssid = ap.ssid;
    link_.bssid = ap.bssid;
    link_.channel = ap.channel;
    link_.rssi = ap.rssi;
    link_.ip = "192.168.1.50";
    return true;
  }
  return false;
}

void Net::wifiOff() { link_ = hal::WifiLink{}; }

std::vector<hal::WifiNetwork> Net::scan() {
  if (!wifiAvailable_) return {};
  std::vector<hal::WifiNetwork> out = world();
  const bool first = scans_++ == 0;
  for (int i = 1; i <= crowd_; ++i) {
    if (first && i % 2 == 0) continue;  // not heard this time
    char name[16], bssid[24];
    std::snprintf(name, sizeof name, "Flat %02d", i);
    std::snprintf(bssid, sizeof bssid, "F1:A7:00:00:00:%02X", i);
    out.push_back({name, -60 - i, i % 3 != 0, 1 + (i * 5) % 13, bssid});
  }
  return out;
}

hal::HttpResponse Net::request(const hal::HttpRequest& req) {
  hal::HttpResponse r;
  if (!link_.connected) {
    r.error = "not connected";
    return r;
  }
  if (!fixturesDir_.empty()) {
    std::string name = fixtureFileName(req.method, req.url, req.body);  // a fixture for this body first, then for the URL
    std::ifstream f(fixturesDir_ + "/" + name, std::ios::binary);
    if (!f && !req.body.empty()) {
      name = fixtureFileName(req.method, req.url);
      f.open(fixturesDir_ + "/" + name, std::ios::binary);
    }
    if (!f) {
      r.error = "no fixture";
      missing_.push_back(req.method + " " + req.url + " -> " + name);
      return r;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    r.body = ss.str();
    r.status = 200;
    std::ifstream st(fixturesDir_ + "/" + name.substr(0, name.size() - 5) + ".status");
    int code;
    if (st >> code) r.status = code;
    return r;
  }
  // Real network through libcurl.
  CURL* c = curl_easy_init();
  if (!c) {
    r.error = "curl init failed";
    return r;
  }
  struct curl_slist* hdrs = nullptr;
  for (const auto& h : req.headers) hdrs = curl_slist_append(hdrs, (h.first + ": " + h.second).c_str());
  curl_easy_setopt(c, CURLOPT_URL, req.url.c_str());
  curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
  if (!req.body.empty()) curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
  curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, static_cast<long>(req.timeoutMs));
  curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeBody);
  curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
  CURLcode rc = curl_easy_perform(c);
  if (rc != CURLE_OK) {
    r.error = curl_easy_strerror(rc);
  } else {
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    r.status = static_cast<int>(code);
  }
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(c);
  return r;
}

int64_t Net::syncTime(uint32_t) { return static_cast<int64_t>(std::time(nullptr)); }

Net::~Net() {
  for (const auto& m : missing_) std::fprintf(stderr, "[fixture] missing: %s\n", m.c_str());
}

}  // namespace pos::sim
