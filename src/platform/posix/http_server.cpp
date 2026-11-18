#include "platform/posix/http_server.h"

// Order matters on the device: Arduino's IPAddress.h declares a variable INADDR_NONE, which lwIP's socket headers make a
// macro, so Arduino comes first; and lwIP's arpa/inet.h is not needed (and breaks before the socket header).
#if defined(ESP_PLATFORM)
#include <Arduino.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#if !defined(ESP_PLATFORM)
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>

#include "core/fs.h"
#include "core/log.h"
#include "core/strings.h"

#if defined(ESP_PLATFORM)
#include "esp_pthread.h"
#endif

namespace pos::platform {

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

// Threads for the server: on the device off the UI core, at the worker's low priority, with room for the file system.
void threadConfig() {
#if defined(ESP_PLATFORM)
  esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
  cfg.stack_size = 8192;
  cfg.prio = 1;
  cfg.pin_to_core = 0;
  cfg.thread_name = "http";
  esp_pthread_set_cfg(&cfg);
#endif
}

const char* reason(int code) {
  switch (code) {
    case 100: return "Continue";
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 207: return "Multi-Status";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 429: return "Too Many Requests";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return code >= 500 ? "Internal Server Error" : "OK";
  }
}

// Sends all of it; false when the connection broke or the client took nothing for the idle limit (a client that stops
// reading must not hold one of the few connection threads for ever).
bool sendAll(int fd, const char* p, size_t n) {
  auto until = std::chrono::steady_clock::now() + std::chrono::seconds(PosixHttpServer::kIdleTimeoutSec);
  while (n > 0) {
    const ssize_t k = ::send(fd, p, n, kSendFlags);
    if (k <= 0) {
      if (k < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      return false;
    }
    p += k;
    n -= static_cast<size_t>(k);
    until = std::chrono::steady_clock::now() + std::chrono::seconds(PosixHttpServer::kIdleTimeoutSec);
  }
  return true;
}

// Buffered reading from one connection.
struct Conn {
  int fd;
  char buf[4096];
  size_t pos = 0, len = 0;
  bool broken = false;

  bool fill() {
    if (pos < len) return true;
    pos = len = 0;
    // lwIP sometimes answers a read with "try again" while data is still coming (the card was busy writing): retry until the
    // connection's idle limit, then give up.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(PosixHttpServer::kIdleTimeoutSec);
    for (;;) {
      const ssize_t k = ::recv(fd, buf, sizeof buf, 0);
      if (k > 0) {
        len = static_cast<size_t>(k);
        return true;
      }
      const int e = errno;
      if (k < 0 && (e == EAGAIN || e == EWOULDBLOCK || e == EINTR) && std::chrono::steady_clock::now() < until) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      if (k < 0) POS_LOG("http", "read failed: errno %d", e);
      broken = true;
      return false;
    }
  }
  // A line without its CRLF; false on a closed connection or one longer than `max`.
  bool line(std::string& out, size_t max) {
    out.clear();
    for (;;) {
      if (!fill()) return false;
      const char c = buf[pos++];
      if (c == '\n') {
        if (!out.empty() && out.back() == '\r') out.pop_back();
        return true;
      }
      out += c;
      if (out.size() > max) return false;
    }
  }
  long some(char* dst, size_t n) {
    if (!fill()) return -1;
    const size_t k = std::min(n, len - pos);
    std::memcpy(dst, buf + pos, k);
    pos += k;
    return static_cast<long>(k);
  }
};

}  // namespace

bool PosixHttpServer::start(hal::ServerHandler handler, uint16_t port) {
  if (running_) return true;
  port_ = port ? port : ownPort_;
  handler_ = std::move(handler);
  listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) return false;
  int yes = 1;
  ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_.load());
  if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(listenFd_, kMaxConnections) != 0) {
    POS_LOG("http", "cannot listen on port %u (errno %d)", static_cast<unsigned>(port_.load()), errno);
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  running_ = true;
  threadConfig();
  acceptThread_ = std::thread([this] { acceptLoop(); });
  POS_LOG("http", "listening on port %u", static_cast<unsigned>(port_.load()));
  return true;
}

void PosixHttpServer::stop() {
  if (!running_.exchange(false)) return;
  if (listenFd_ >= 0) {
    ::shutdown(listenFd_, SHUT_RDWR);
    ::close(listenFd_);
    listenFd_ = -1;
  }
  if (acceptThread_.joinable()) acceptThread_.join();
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (int fd : clients_) ::shutdown(fd, SHUT_RDWR);  // the connection threads close them
  }
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (active_ > 0 && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  port_ = ownPort_;
  POS_LOG("http", "stopped");
}

void PosixHttpServer::acceptLoop() {
  threadConfig();
  while (running_) {
    const int lfd = listenFd_;
    if (lfd < 0) break;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(lfd, &set);
    timeval tv{0, 300 * 1000};
    if (::select(lfd + 1, &set, nullptr, nullptr, &tv) <= 0) continue;
    const int fd = ::accept(lfd, nullptr, nullptr);
    if (fd < 0) continue;
    if (active_ >= kMaxConnections || !running_) {
      static const char kBusy[] = "HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
      sendAll(fd, kBusy, sizeof kBusy - 1);
      ::close(fd);
      continue;
    }
    timeval to{kIdleTimeoutSec, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof to);
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#if defined(SO_NOSIGPIPE)
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    {
      std::lock_guard<std::mutex> lock(mu_);
      clients_.push_back(fd);
    }
    ++active_;
    std::thread([this, fd] {
      serve(fd);
      {
        std::lock_guard<std::mutex> lock(mu_);
        clients_.erase(std::remove(clients_.begin(), clients_.end(), fd), clients_.end());
      }
      ::close(fd);
      --active_;
    }).detach();
  }
}

void PosixHttpServer::serve(int fd) {
  auto conn = std::make_unique<Conn>();
  conn->fd = fd;
  while (running_) {
    std::string first;
    if (!conn->line(first, 4096) || first.empty()) return;
    hal::ServerRequest req;
    const size_t a = first.find(' '), b = first.rfind(' ');
    if (a == std::string::npos || b <= a) return;
    req.method = first.substr(0, a);
    req.target = first.substr(a + 1, b - a - 1);
    const std::string version = first.substr(b + 1);
    size_t headerBytes = 0;
    bool headersEnded = false;
    for (std::string h; conn->line(h, 8192);) {
      if (h.empty()) {
        headersEnded = true;
        break;
      }
      headerBytes += h.size();
      if (headerBytes > 16384 || req.headers.size() > 100) return;
      const auto colon = h.find(':');
      if (colon == std::string::npos) continue;
      req.headers.emplace_back(lowerAscii(trimmed(h.substr(0, colon))), trimmed(h.substr(colon + 1)));
    }
    if (!headersEnded) return;  // the connection broke or a header line is too long: a request cut short is not run
    const std::string te = lowerAscii(req.header("transfer-encoding"));
    req.chunked = te.find("chunked") != std::string::npos;
    const std::string cl = req.header("content-length");
    if (!req.chunked && !cl.empty()) req.contentLength = std::strtoll(cl.c_str(), nullptr, 10);
    if (lowerAscii(req.header("expect")) == "100-continue") {
      static const char kContinue[] = "HTTP/1.1 100 Continue\r\n\r\n";
      if (!sendAll(fd, kContinue, sizeof kContinue - 1)) return;
    }

    // The body: Content-Length bytes, or chunks until the empty one; nothing for neither.
    int64_t left = req.chunked ? 0 : std::max<int64_t>(0, req.contentLength);
    bool chunkDone = !req.chunked, bodyBroken = false;
    Conn* c = conn.get();
    req.read = [c, &left, &chunkDone, &bodyBroken, chunked = req.chunked](char* dst, size_t n) -> long {
      if (bodyBroken) return -1;
      if (chunked && left == 0 && !chunkDone) {
        std::string size;
        if (!c->line(size, 64)) {
          bodyBroken = true;
          return -1;
        }
        if (size.empty() && !c->line(size, 64)) {  // the CRLF that ends the previous chunk
          bodyBroken = true;
          return -1;
        }
        left = static_cast<int64_t>(std::strtoull(size.c_str(), nullptr, 16));
        if (left == 0) {  // the last chunk: trailers up to an empty line
          chunkDone = true;
          for (std::string t; c->line(t, 4096) && !t.empty();) {
          }
          return 0;
        }
      }
      if (left <= 0) return 0;
      const long k = c->some(dst, static_cast<size_t>(std::min<int64_t>(left, static_cast<int64_t>(n))));
      if (k <= 0) {
        bodyBroken = true;
        return -1;
      }
      left -= k;
      return k;
    };

    hal::ServerResponse res = handler_(req);
    char sink[1024];
    while (req.read(sink, sizeof sink) > 0) {  // what the handler left unread
    }
    const bool keep = !bodyBroken && version == "HTTP/1.1" && lowerAscii(req.header("connection")) != "close" && running_;

    const uint64_t length = res.file.empty() ? res.body.size() : res.fileSize;
    std::string head = "HTTP/1.1 " + std::to_string(res.status) + " " + reason(res.status) + "\r\n";
    for (const auto& h : res.headers) head += h.first + ": " + h.second + "\r\n";
    head += "Content-Length: " + std::to_string(length) + "\r\n";
    head += keep ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";
    if (!sendAll(fd, head.data(), head.size())) return;
    if (!res.headOnly) {
      if (res.file.empty()) {
        if (!sendAll(fd, res.body.data(), res.body.size())) return;
      } else {
        fs::Reader in;
        if (!in.open(res.file)) return;
        std::vector<char> buf(8192);
        for (uint64_t at = 0; at < res.fileSize;) {
          const size_t n = static_cast<size_t>(std::min<uint64_t>(buf.size(), res.fileSize - at));
          if (!in.readAt(at, buf.data(), n) || !sendAll(fd, buf.data(), n)) return;
          at += n;
        }
      }
    }
    if (!keep) return;
  }
}

}  // namespace pos::platform
