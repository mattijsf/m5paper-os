#include "core/ha.h"

#include "core/log.h"
#include "core/net.h"
#include "core/secrets.h"

namespace pos {

const char* haErrorText(HaError e) {
  switch (e) {
    case HaError::None: return "OK";
    case HaError::NoToken: return "No token on the card";
    case HaError::NoUrl: return "No Home Assistant address";
    case HaError::NoWifi: return "No Wi-Fi";
    case HaError::Timeout: return "Home Assistant did not answer";
    case HaError::Unauthorized: return "Token rejected";
    case HaError::NotFound: return "Not found in Home Assistant";
    case HaError::Server: return "Home Assistant error";
    default: return "Home Assistant not reachable";
  }
}

HaClient HaClient::fromNet(NetService& net, const Secrets& secrets) {
  NetService* n = &net;
  return HaClient([n](const hal::HttpRequest& r) { return n->perform(r); }, net.haBaseUrl(), secrets.get("HA_TOKEN"));
}

HaResult HaClient::request(const std::string& method, const std::string& path, const std::string& body) {
  HaResult r;
  if (token_.empty()) {
    r.error = HaError::NoToken;
    return r;
  }
  if (base_.empty()) {
    r.error = HaError::NoUrl;
    return r;
  }
  hal::HttpRequest req;
  req.method = method;
  req.url = base_ + path;
  req.body = body;
  req.timeoutMs = timeoutMs_;
  req.headers.push_back({"Authorization", "Bearer " + token_});
  req.headers.push_back({"Content-Type", "application/json"});
  hal::HttpResponse resp = send_(req);
  r.status = resp.status;
  r.body = std::move(resp.body);
  r.detail = resp.error;
  if (resp.status == 0) {
    const std::string& e = resp.error;
    if (e.rfind("No Wi-Fi", 0) == 0 || e == "not connected") r.error = HaError::NoWifi;
    else if (e.find("timeout") != std::string::npos || e.find("timed out") != std::string::npos || e.find("Timeout") != std::string::npos) r.error = HaError::Timeout;
    else r.error = HaError::Other;
  } else if (resp.status == 401 || resp.status == 403) {
    r.error = HaError::Unauthorized;
  } else if (resp.status == 404) {
    r.error = HaError::NotFound;
  } else if (resp.status >= 500) {
    r.error = HaError::Server;
  } else if (resp.status < 200 || resp.status >= 300) {
    r.error = HaError::Other;
  }
  if (r.error != HaError::None) POS_LOG("ha", "%s %s -> %d (%s%s%s)", method.c_str(), path.c_str(), r.status, haErrorText(r.error), r.detail.empty() ? "" : ": ", r.detail.c_str());  // path only: no token (detail is the transport's error text)
  return r;
}

HaResult HaClient::get(const std::string& path) { return request("GET", path, ""); }
HaResult HaClient::post(const std::string& path, const std::string& json) { return request("POST", path, json); }

HaResult HaClient::callService(const std::string& domain, const std::string& service, const std::string& json, bool wantResponse) {
  return post("/api/services/" + domain + "/" + service + (wantResponse ? "?return_response" : ""), json.empty() ? "{}" : json);
}

std::vector<HaResult> HaClient::getMany(const std::vector<std::string>& paths) {
  std::vector<HaResult> out;
  out.reserve(paths.size());
  HaError fatal = HaError::None;
  for (const std::string& p : paths) {
    if (fatal != HaError::None) {
      HaResult r;
      r.error = fatal;
      out.push_back(r);
      continue;
    }
    out.push_back(get(p));
    const HaError e = out.back().error;
    if (e == HaError::NoToken || e == HaError::NoUrl || e == HaError::NoWifi || e == HaError::Unauthorized) fatal = e;
  }
  return out;
}

}  // namespace pos
