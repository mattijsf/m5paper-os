#include <doctest.h>

#include <string>
#include <vector>

#include "core/ha.h"

using namespace pos;

namespace {

struct Wire {
  std::vector<hal::HttpRequest> sent;
  hal::HttpResponse reply;
  std::vector<hal::HttpResponse> queue;  // per request, if not empty
  HaClient client(std::string base = "https://ha.example.test", std::string token = "tok-secret") {
    return HaClient(
        [this](const hal::HttpRequest& r) {
          sent.push_back(r);
          if (!queue.empty()) {
            auto x = queue.front();
            queue.erase(queue.begin());
            return x;
          }
          return reply;
        },
        std::move(base), std::move(token));
  }
};

hal::HttpResponse http(int status, std::string body = "", std::string err = "") {
  hal::HttpResponse r;
  r.status = status;
  r.body = std::move(body);
  r.error = std::move(err);
  return r;
}

}  // namespace

TEST_CASE("ha: a good GET sends the bearer token and returns the body") {
  Wire w;
  w.reply = http(200, "{\"message\":\"API running.\"}");
  HaResult r = w.client().get("/api/");
  CHECK(r.ok());
  CHECK(r.status == 200);
  CHECK(r.body == "{\"message\":\"API running.\"}");
  REQUIRE(w.sent.size() == 1);
  CHECK(w.sent[0].method == "GET");
  CHECK(w.sent[0].url == "https://ha.example.test/api/");
  bool auth = false, json = false;
  for (const auto& h : w.sent[0].headers) {
    if (h.first == "Authorization" && h.second == "Bearer tok-secret") auth = true;
    if (h.first == "Content-Type" && h.second == "application/json") json = true;
  }
  CHECK(auth);
  CHECK(json);
}

TEST_CASE("ha: every failure has its own error") {
  Wire w;
  struct Case {
    hal::HttpResponse resp;
    HaError want;
  } cases[] = {
      {http(0, "", "No Wi-Fi: no known network in range"), HaError::NoWifi},
      {http(0, "", "not connected"), HaError::NoWifi},
      {http(0, "", "read timeout"), HaError::Timeout},
      {http(0, "", "connection timed out"), HaError::Timeout},
      {http(0, "", "TLS handshake failed"), HaError::Other},
      {http(401), HaError::Unauthorized},
      {http(403), HaError::Unauthorized},
      {http(404, "{\"message\":\"Entity not found.\"}"), HaError::NotFound},
      {http(500), HaError::Server},
      {http(503), HaError::Server},
      {http(400), HaError::Other},
      {http(204), HaError::None},
  };
  for (const Case& c : cases) {
    w.reply = c.resp;
    HaResult r = w.client().get("/api/states/light.x");
    CHECK(r.error == c.want);
    CHECK(r.ok() == (c.want == HaError::None));
    CHECK(std::string(haErrorText(r.error)).size() > 0);
  }
  // 404 keeps the body (it explains)
  w.reply = http(404, "{\"message\":\"Entity not found.\"}");
  CHECK(w.client().get("/x").body.find("not found") != std::string::npos);
}

TEST_CASE("ha: no token or no address means no request at all") {
  Wire w;
  w.reply = http(200);
  CHECK(w.client("https://x", "").get("/api/").error == HaError::NoToken);
  CHECK(w.client("", "t").get("/api/").error == HaError::NoUrl);
  CHECK(w.sent.empty());
}

TEST_CASE("ha: callService posts the data to /api/services/<domain>/<service>") {
  Wire w;
  w.reply = http(200, "[]");
  CHECK(w.client().callService("light", "turn_on", "{\"entity_id\":\"light.lounge\",\"brightness_pct\":60}").ok());
  REQUIRE(w.sent.size() == 1);
  CHECK(w.sent[0].method == "POST");
  CHECK(w.sent[0].url == "https://ha.example.test/api/services/light/turn_on");
  CHECK(w.sent[0].body == "{\"entity_id\":\"light.lounge\",\"brightness_pct\":60}");
  w.client().callService("scene", "turn_on", "", true);
  CHECK(w.sent[1].url == "https://ha.example.test/api/services/scene/turn_on?return_response");
  CHECK(w.sent[1].body == "{}");
}

TEST_CASE("ha: getMany goes through every path; errors that would repeat stop the requests") {
  Wire w;
  w.queue = {http(200, "a"), http(404), http(200, "c")};
  auto r = w.client().getMany({"/1", "/2", "/3"});
  REQUIRE(r.size() == 3);
  CHECK(r[0].ok());
  CHECK(r[1].error == HaError::NotFound);
  CHECK(r[2].ok());
  CHECK(w.sent.size() == 3);

  Wire v;
  v.queue = {http(200, "a"), http(401), http(200, "c")};
  auto q = v.client().getMany({"/1", "/2", "/3", "/4"});
  REQUIRE(q.size() == 4);
  CHECK(q[1].error == HaError::Unauthorized);
  CHECK(q[2].error == HaError::Unauthorized);  // not asked: it would be 401 again
  CHECK(q[3].error == HaError::Unauthorized);
  CHECK(v.sent.size() == 2);

  Wire n;
  n.reply = http(0, "", "No Wi-Fi: nothing in range");
  auto o = n.client().getMany({"/1", "/2"});
  CHECK(o[0].error == HaError::NoWifi);
  CHECK(o[1].error == HaError::NoWifi);
  CHECK(n.sent.size() == 1);
  CHECK(w.client().getMany({}).empty());
}
