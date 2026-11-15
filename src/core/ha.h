// Home Assistant REST client (docs/BACKEND.md "API calls used"). Blocking: use it on the worker thread (ctx.async). Used by the
// Home app and the inbox. Only the non-admin API subset: GET /api/, GET /api/states/<id>, POST /api/services/<domain>/<service>.
// Never /api/states (hundreds of KB), /api/template, or events: the token may belong to a dedicated non-admin user (STATUS.md).
// The token is sent as a Bearer header and never logged, stored or shown.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "hal/net.h"

namespace pos {

class NetService;
class Secrets;

enum class HaError : uint8_t {
  None,
  NoToken,       // HA_TOKEN missing on the card
  NoUrl,         // no base URL (HA_URL on the card, or config)
  NoWifi,        // the net service could not bring Wi-Fi up
  Timeout,       // the request ran out of time
  Unauthorized,  // 401 or 403: the token is wrong or revoked
  NotFound,      // 404: no such entity or service
  Server,        // 5xx
  Other,         // anything else (another status, a transport error)
};

struct HaResult {
  HaError error = HaError::None;
  int status = 0;     // HTTP status, 0 = no response
  std::string body;   // response body (also for errors, it may explain)
  std::string detail; // transport error text from the net layer, "" if none
  bool ok() const { return error == HaError::None; }
};

// One line for the user ("No Wi-Fi", "Token rejected", ...).
const char* haErrorText(HaError e);

class HaClient {
 public:
  using Transport = std::function<hal::HttpResponse(const hal::HttpRequest&)>;
  // `baseUrl` without a trailing slash. `transport` sends the request (NetService::perform on the device).
  HaClient(Transport transport, std::string baseUrl, std::string token, uint32_t timeoutMs = 15000)
      : send_(std::move(transport)), base_(std::move(baseUrl)), token_(std::move(token)), timeoutMs_(timeoutMs) {}
  // The usual client: NetService for the transport, HA_URL / HA_TOKEN from the card (config.json for the URL).
  static HaClient fromNet(NetService& net, const Secrets& secrets);

  HaResult get(const std::string& path);
  HaResult post(const std::string& path, const std::string& json);
  // POST /api/services/<domain>/<service>; `json` is the service data, e.g. {"entity_id":"light.x","brightness_pct":60}.
  // wantResponse adds ?return_response (the body then holds changed_states and service_response).
  HaResult callService(const std::string& domain, const std::string& service, const std::string& json, bool wantResponse = false);
  // Several GETs in a row (a refresh of a tab): one result per path, in order. Every request of one refresh goes out back to
  // back so the net layer keeps the TLS connection alive between them. A failure of one does not stop the others, except
  // NoToken / NoUrl / NoWifi / Unauthorized, which would repeat: the rest then get the same error without a request.
  std::vector<HaResult> getMany(const std::vector<std::string>& paths);

 private:
  HaResult request(const std::string& method, const std::string& path, const std::string& body);
  Transport send_;
  std::string base_, token_;
  uint32_t timeoutMs_;
};

}  // namespace pos
