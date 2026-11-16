#include "core/http_fixture.h"

#include <cstdint>
#include <cstdio>

namespace pos {

namespace {
uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) {
    h ^= c;
    h *= 16777619u;
  }
  return h;
}
}  // namespace

std::string fixtureFileName(const std::string& method, const std::string& url) {
  std::string rest = url;
  size_t scheme = rest.find("://");
  if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
  std::string query;
  size_t q = rest.find('?');
  if (q != std::string::npos) {
    query = rest.substr(q + 1);
    rest = rest.substr(0, q);
  }
  size_t slash = rest.find('/');
  std::string host = slash == std::string::npos ? rest : rest.substr(0, slash);
  std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
  size_t colon = host.find(':');
  if (colon != std::string::npos) host = host.substr(0, colon);
  for (char& c : path)
    if (c == '/') c = '_';
  std::string name = method + "_" + host + path;
  if (!query.empty()) {
    char buf[12];
    std::snprintf(buf, sizeof buf, "_%08x", static_cast<unsigned>(fnv1a(query)));
    name += buf;
  }
  return name + ".json";
}

std::string fixtureFileName(const std::string& method, const std::string& url, const std::string& body) {
  std::string name = fixtureFileName(method, url);
  if (body.empty()) return name;
  char buf[12];
  std::snprintf(buf, sizeof buf, "_b%08x", static_cast<unsigned>(fnv1a(body)));
  return name.substr(0, name.size() - 5) + buf + ".json";
}

}  // namespace pos
