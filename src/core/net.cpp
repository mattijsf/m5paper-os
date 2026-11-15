#include "core/net.h"

#include "core/display_manager.h"  // kNoDeadline
#include "core/log.h"

namespace pos {

namespace {
constexpr uint32_t kFastTimeoutMs = 5000;
constexpr uint32_t kScanConnectTimeoutMs = 10000;
}  // namespace

std::string NetService::haBaseUrl() const {
  std::string url = secrets_.get("HA_URL");
  if (url.empty()) url = config_.ha.baseUrl;
  while (!url.empty() && url.back() == '/') url.pop_back();
  return url;
}

void NetService::markActive() { lastActiveMs_ = p_.clock.millis(); }

void NetService::succeeded(Result& r, bool fast) {
  hal::WifiLink l = p_.net.link();
  r.ok = true;
  r.fast = fast;
  r.ssid = l.ssid.empty() ? r.ssid : l.ssid;
  store_.remember(r.ssid, l.bssid, l.channel);
  store_.save();
  markActive();
  // One clock sync per day, or when the clock was never set; the result is applied on the UI thread (tick()).
  const int64_t now = p_.clock.now();
  if (now <= 0 || now - lastSyncUnix_ > 24 * 3600) {
    const int64_t t = p_.net.syncTime(5000);
    if (t > 0) {
      pendingTime_ = t;
      lastSyncUnix_ = t;
    }
  }
}

NetService::Result NetService::connectLocked() {
  Result r;
  const uint32_t t0 = p_.clock.millis();
  auto finish = [&](Result res) {
    res.ms = p_.clock.millis() - t0;
    last_ = res;
    POS_LOG("net", "%s ssid=%s fast=%d %ums%s%s", res.ok ? "connected" : "FAILED", res.ssid.c_str(), res.fast ? 1 : 0,
            static_cast<unsigned>(res.ms), res.error.empty() ? "" : ": ", res.error.c_str());
    if (!res.scan.empty()) POS_LOG("net", "scan: %s", res.scan.c_str());
    return res;
  };

  if (p_.net.wifiConnected()) {
    r.ok = true;
    r.ssid = p_.net.link().ssid;
    markActive();
    return r;  // already up: not an attempt, not logged
  }
  const std::vector<WifiCred> kn = known();
  if (kn.empty()) {
    r.error = "No Wi-Fi networks configured";
    return finish(r);
  }

  // Attempt 1: the network that worked last, straight to its access point.
  if (auto fast = fastCandidate(kn, store_.last())) {
    r.ssid = fast->ssid;
    auto mem = store_.memoryOf(fast->ssid);
    if (p_.net.wifiConnect(fast->ssid, fast->password, kFastTimeoutMs, mem ? mem->bssid : "", mem ? mem->channel : 0)) {
      succeeded(r, true);
      return finish(r);
    }
  }

  // Attempt 2: scan once, join the known network with the strongest signal.
  auto aps = p_.net.scan();
  r.scan = scanSummary(aps);
  auto pick = strongestKnown(kn, aps);
  if (!pick) {
    r.error = "No known network in range";
    return finish(r);
  }
  r.ssid = pick->cred.ssid;
  if (p_.net.wifiConnect(pick->cred.ssid, pick->cred.password, kScanConnectTimeoutMs, pick->ap.bssid, pick->ap.channel)) {
    succeeded(r, false);
    return finish(r);
  }
  r.error = "Could not join " + pick->cred.ssid;
  return finish(r);
}

NetService::Result NetService::ensureConnected() {
  std::lock_guard<std::mutex> lock(mu_);
  return connectLocked();
}

hal::HttpResponse NetService::perform(const hal::HttpRequest& req) {
  std::lock_guard<std::mutex> lock(mu_);
  ++inFlight_;
  hal::HttpResponse resp;
  Result r = connectLocked();
  if (!r.ok) {
    resp.error = "No Wi-Fi: " + r.error;
  } else {
    resp = p_.net.request(req);
  }
  markActive();
  --inFlight_;
  return resp;
}

NetService::Result NetService::connectTo(const std::string& ssid, const std::string& password, bool save) {
  std::lock_guard<std::mutex> lock(mu_);
  ++inFlight_;
  Result r;
  r.ssid = ssid;
  const uint32_t t0 = p_.clock.millis();
  if (p_.net.wifiConnected()) p_.net.wifiOff();
  if (p_.net.wifiConnect(ssid, password, kScanConnectTimeoutMs)) {
    succeeded(r, false);
    if (save) {
      store_.add(ssid, password);
      store_.save();
    }
  } else {
    r.error = "Could not join " + ssid;
  }
  r.ms = p_.clock.millis() - t0;
  last_ = r;
  POS_LOG("net", "%s ssid=%s %ums%s%s", r.ok ? "connected" : "FAILED", ssid.c_str(), static_cast<unsigned>(r.ms),
          r.error.empty() ? "" : ": ", r.error.c_str());
  --inFlight_;
  return r;
}

std::vector<hal::WifiNetwork> NetService::scan() {
  std::lock_guard<std::mutex> lock(mu_);
  ++inFlight_;
  auto aps = p_.net.scan();
  markActive();
  --inFlight_;
  return aps;
}

void NetService::tick() {
  const int64_t t = pendingTime_.exchange(0);
  bool changed = false;
  if (t > 0) {
    p_.clock.set(t);
    changed = true;
  }
  const bool on = p_.net.wifiConnected();
  if (on != wasOnline_) {
    wasOnline_ = on;
    changed = true;
  }
  if (on && inFlight_ == 0) {
    const uint32_t idle = p_.clock.millis() - lastActiveMs_;
    if (idle >= static_cast<uint32_t>(config_.net.lingerSec) * 1000u && mu_.try_lock()) {
      p_.net.wifiOff();  // after the linger time: Wi-Fi off
      mu_.unlock();
      POS_LOG("net", "Wi-Fi off after %us idle", static_cast<unsigned>(idle / 1000));
      wasOnline_ = false;
      changed = true;
    }
  }
  if (changed && onChange) onChange();
}

uint32_t NetService::msToNextDeadline() {
  if (!p_.net.wifiConnected() || inFlight_ > 0) return kNoDeadline;
  const uint32_t lingerMs = static_cast<uint32_t>(config_.net.lingerSec) * 1000u;
  const uint32_t idle = p_.clock.millis() - lastActiveMs_;
  return idle >= lingerMs ? 0 : lingerMs - idle;
}

bool NetService::online() { return p_.net.wifiConnected(); }
hal::WifiLink NetService::link() { return p_.net.link(); }

void NetService::disconnect() {
  std::lock_guard<std::mutex> lock(mu_);
  p_.net.wifiOff();
}

NetService::Result NetService::lastResult() {
  std::lock_guard<std::mutex> lock(mu_);
  return last_;
}

}  // namespace pos
