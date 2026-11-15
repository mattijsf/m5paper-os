#include "core/http_fixture.h"

#include <cstdint>
#include <cstdio>

namespace pos {

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
    uint32_t h = 2166136261u;
    for (unsigned char c : query) {
      h ^= c;
      h *= 16777619u;
    }
    char buf[12];
    std::snprintf(buf, sizeof buf, "_%08x", static_cast<unsigned>(h));
    name += buf;
  }
  return name + ".json";
}

}  // namespace pos
