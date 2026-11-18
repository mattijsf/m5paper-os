// HAL: a small HTTP/1.1 server on the local network (docs/apps/settings.md "Files over Wi-Fi"). One implementation on BSD
// sockets serves both platforms (platform/posix/http_server); the requests are answered by shared code (core/file_server).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace pos::hal {

struct ServerRequest {
  std::string method;  // "GET", "PROPFIND", ...
  std::string target;  // the path with its query, as sent ("/Alice%20in.epub?x")
  std::vector<std::pair<std::string, std::string>> headers;  // names in lower case; may hold an Authorization header: never log it
  int64_t contentLength = -1;  // -1: not given (or chunked)
  bool chunked = false;
  // Reads up to n bytes of the body (chunked transfer decoded): the bytes read, 0 at its end, -1 on an error.
  std::function<long(char*, size_t)> read;

  std::string header(const std::string& lowerName) const {
    for (const auto& h : headers)
      if (h.first == lowerName) return h.second;
    return "";
  }
};

struct ServerResponse {
  int status = 200;
  std::vector<std::pair<std::string, std::string>> headers;  // Content-Length is added by the server
  std::string body;
  std::string file;       // a virtual path (core/fs) streamed as the body instead; "" for none
  uint64_t fileSize = 0;  // its length
  bool headOnly = false;  // HEAD: headers and length, no body
};

using ServerHandler = std::function<ServerResponse(ServerRequest&)>;

class HttpServer {
 public:
  virtual ~HttpServer() = default;
  // Listens on all interfaces; the handler runs on the server's own threads (one per connection, a few at most).
  // `port` 0: the platform's own (below); another number for a service of its own (the update page: 8266).
  virtual bool start(ServerHandler handler, uint16_t port = 0) = 0;
  virtual void stop() = 0;  // closes every connection; returns when no handler runs any more
  virtual uint16_t port() const = 0;  // the port it listens on, or would with start(handler, 0): device 80, simulator 8080
};

}  // namespace pos::hal
