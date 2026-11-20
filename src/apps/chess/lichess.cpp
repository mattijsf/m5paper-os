#include "apps/chess/lichess.h"

#include <ArduinoJson.h>

namespace pos::lichess {

namespace {

hal::HttpRequest base(const std::string& token, const std::string& method, const std::string& path) {
  hal::HttpRequest r;
  r.method = method;
  r.url = std::string(kBase) + path;
  r.headers.push_back({"Authorization", "Bearer " + token});
  r.timeoutMs = 15000;
  return r;
}

bool validId(const std::string& id) {
  if (id.empty() || id.size() > 16) return false;
  for (char c : id)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  return true;
}

}  // namespace

hal::HttpRequest challengeAi(const std::string& token, int level, bool white) {
  hal::HttpRequest r = base(token, "POST", "/api/challenge/ai");
  r.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});
  r.body = "level=" + std::to_string(level < 1 ? 1 : level > 8 ? 8 : level) + "&color=" + (white ? "white" : "black");
  return r;
}

hal::HttpRequest playing(const std::string& token) { return base(token, "GET", "/api/account/playing?nb=50"); }

hal::HttpRequest move(const std::string& token, const std::string& gameId, const std::string& uci) {
  return base(token, "POST", "/api/board/game/" + gameId + "/move/" + uci);
}

hal::HttpRequest resign(const std::string& token, const std::string& gameId) {
  return base(token, "POST", "/api/board/game/" + gameId + "/resign");
}

hal::HttpRequest game(const std::string& token, const std::string& gameId) {
  hal::HttpRequest r = base(token, "GET", "/game/export/" + gameId + "?moves=false&clocks=false&evals=false&opening=false");
  r.headers.push_back({"Accept", "application/json"});
  return r;
}

std::string gameIdOf(const std::string& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "";
  const std::string id = doc["id"] | "";
  return validId(id) ? id : "";
}

bool parsePlaying(const std::string& body, const std::string& gameId, Playing& out) {
  JsonDocument doc;
  if (deserializeJson(doc, body) || !doc["nowPlaying"].is<JsonArrayConst>()) return false;
  out = Playing{};
  for (JsonObjectConst g : doc["nowPlaying"].as<JsonArrayConst>()) {
    if (std::string(g["gameId"] | "") != gameId) continue;
    out.found = true;
    out.fen = g["fen"] | "";
    out.lastMove = g["lastMove"] | "";
    out.myTurn = g["isMyTurn"] | false;
    break;
  }
  return true;
}

bool parseGame(const std::string& body, Ended& out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  out.status = doc["status"] | "";
  out.winner = doc["winner"] | "";
  return !out.status.empty();
}

std::string errorOf(const std::string& body) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return "";
  if (doc["error"].is<const char*>()) return doc["error"].as<std::string>();
  return "";
}

std::string problemText(const hal::HttpResponse& r) {
  if (r.status == 0) return "Geen verbinding met Lichess.";
  if (r.status == 401) return "Lichess kent dit token niet (LICHESS_TOKEN op de kaart).";
  if (r.status == 403) return "Het token mag dit niet: maak er een met board:play en challenge:write.";
  if (r.status == 429) return "Lichess vraagt even te wachten. Probeer het over een minuut.";
  const std::string e = errorOf(r.body);
  return e.empty() ? "Lichess antwoordde " + std::to_string(r.status) + "." : "Lichess: " + e;
}

}  // namespace pos::lichess
