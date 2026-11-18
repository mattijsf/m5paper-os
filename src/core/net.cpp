#include "core/net.h"

#include <chrono>
#include <thread>

#include "core/display_manager.h"  // kNoDeadline
#include "core/log.h"
#include "core/timefmt.h"

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
  connected_ = true;
  cacheLink();
  r.ok = true;
  r.fast = fast;
  r.joined = true;
  r.ssid = l.ssid.empty() ? r.ssid : l.ssid;
  store_.remember(r.ssid, l.bssid, l.channel);
  store_.save();
  markActive();
  // One clock sync per day, or when the clock was never set. It runs as its own worker job AFTER the request that
  // brought Wi-Fi up (a blocking SNTP wait must not delay that request); the result is applied by tick().
  const int64_t now = p_.clock.now();
  if (now <= 0 || now - lastSyncUnix_ > 24 * 3600) needSync_ = true;
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
    connected_ = true;
    cacheLink();
    r.ok = true;
    r.ssid = linkCache_.ssid;
    markActive();
    return r;  // already up: not an attempt, not logged
  }
  connected_ = false;
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
  const uint32_t tEnter = p_.clock.millis();
  std::lock_guard<std::mutex> lock(mu_);
  const uint32_t tLocked = p_.clock.millis();
  ++inFlight_;
  hal::HttpResponse resp;
  Result r = connectLocked();
  const uint32_t tConnected = p_.clock.millis();
  if (!r.ok) {
    resp.error = "No Wi-Fi: " + r.error;
  } else {
    resp = p_.net.request(req);
    if (resp.status == 0) {
      trouble_ = Trouble{resp.error, p_.clock.now(), p_.clock.millis() - tConnected, trouble_.count + 1, r.joined, false, false};
    }
    if (resp.status == 0 && resp.notSent && p_.clock.millis() - tConnected < kRetryWithinMs) {
      // Failed at once and nothing left the device: one more try (net.h). Seen as a "Home Assistant not reachable" toast
      // when an app was opened with Wi-Fi off.
      POS_LOG("net", "request not sent (%s) %ums after %s: one more try", resp.error.c_str(), static_cast<unsigned>(p_.clock.millis() - tConnected),
              r.joined ? "the join" : "the start, Wi-Fi was up");
      trouble_.retried = true;
      if (!p_.net.wifiConnected()) {
        connected_ = false;
        r = connectLocked();  // the link fell: the usual one fast and one scan attempt
      } else if (r.joined && retryPauseMs_ > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(retryPauseMs_));
      }
      if (r.ok) {
        resp = p_.net.request(req);
        trouble_.recovered = resp.status != 0;
      } else {
        resp = hal::HttpResponse{};
        resp.error = "No Wi-Fi: " + r.error;
      }
    }
  }
  markActive();
  if (r.ok && !p_.net.wifiConnected()) connected_ = false;  // the link dropped during the request
  else if (r.ok) cacheLink();
  --inFlight_;
  POS_LOG("net", "perform: lock wait %ums, connect %ums, request %ums (start t=%u, end t=%u)", static_cast<unsigned>(tLocked - tEnter),
          static_cast<unsigned>(tConnected - tLocked), static_cast<unsigned>(p_.clock.millis() - tConnected), static_cast<unsigned>(tEnter), static_cast<unsigned>(p_.clock.millis()));
  return resp;
}

NetService::Result NetService::connectTo(const std::string& ssid, const std::string& password, bool save) {
  std::lock_guard<std::mutex> lock(mu_);
  ++inFlight_;
  Result r;
  r.ssid = ssid;
  const uint32_t t0 = p_.clock.millis();
  if (p_.net.wifiConnected()) p_.net.wifiOff();
  connected_ = false;
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

void NetService::syncClock() {
  std::lock_guard<std::mutex> lock(mu_);
  if (!connected_) return;
  const uint32_t t0 = p_.clock.millis();
  const int64_t t = p_.net.syncTime(5000);
  POS_LOG("net", "time sync %s in %ums", t > 0 ? "ok" : "failed", static_cast<unsigned>(p_.clock.millis() - t0));
  if (t > 0) {
    pendingTime_ = t;
    lastSyncUnix_ = t;
    markActive();
  }
}

void NetService::tick() {
  if (needSync_ && inFlight_ == 0 && worker_) {
    needSync_ = false;
    worker_->async([this] { syncClock(); }, [] {});
  }
  const int64_t t = pendingTime_.exchange(0);
  bool changed = false;
  if (t > 0) {
    p_.clock.set(t);
    applyTimezone(config_.net.timezone);  // wall time is formatted in the configured zone; the RTC stays UTC
    changed = true;
  }
  const bool on = connected_;
  const bool cameUp = on && !wasOnline_;
  if (on != wasOnline_) {
    wasOnline_ = on;
    changed = true;
  }
  if (on && inFlight_ == 0) {
    const uint32_t idle = p_.clock.millis() - lastActiveMs_;
    if (idle >= static_cast<uint32_t>(config_.net.lingerSec) * 1000u && mu_.try_lock()) {
      p_.net.wifiOff();  // after the linger time: Wi-Fi off
      connected_ = false;
      mu_.unlock();
      POS_LOG("net", "Wi-Fi off after %us idle", static_cast<unsigned>(idle / 1000));
      wasOnline_ = false;
      changed = true;
    }
  }
  if (changed && onChange) onChange();
  if (cameUp && connected_ && onConnected) onConnected();
}

uint32_t NetService::msToNextDeadline() {
  if (!connected_ || inFlight_ > 0) return kNoDeadline;
  const uint32_t lingerMs = static_cast<uint32_t>(config_.net.lingerSec) * 1000u;
  const uint32_t idle = p_.clock.millis() - lastActiveMs_;
  return idle >= lingerMs ? 0 : lingerMs - idle;
}

void NetService::cacheLink() {
  hal::WifiLink l = p_.net.link();
  std::lock_guard<std::mutex> lock(linkMu_);
  linkCache_ = l;
}

hal::WifiLink NetService::link() {
  std::lock_guard<std::mutex> lock(linkMu_);
  return connected_ ? linkCache_ : hal::WifiLink{};
}

void NetService::disconnect() {
  std::lock_guard<std::mutex> lock(mu_);
  p_.net.wifiOff();
  connected_ = false;
}

NetService::Trouble NetService::lastTrouble() {
  std::lock_guard<std::mutex> lock(mu_);
  return trouble_;
}

NetService::Result NetService::lastResult() {
  std::lock_guard<std::mutex> lock(mu_);
  return last_;
}

}  // namespace pos
