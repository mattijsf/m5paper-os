// hal::HttpServer on BSD sockets, the same code on the device (lwIP) and in the simulator (macOS): HTTP/1.1 with keep-alive,
// chunked request bodies (macOS Finder sends its uploads chunked, which ESP-IDF's own server cannot read), "Expect:
// 100-continue", files streamed from core/fs. One thread per connection, at most kMaxConnections. Built by both platform mains.
#pragma once

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "hal/http_server.h"

namespace pos::platform {

class PosixHttpServer : public hal::HttpServer {
 public:
  explicit PosixHttpServer(uint16_t port) : ownPort_(port), port_(port) {}
  ~PosixHttpServer() override { stop(); }
  bool start(hal::ServerHandler handler, uint16_t port = 0) override;
  void stop() override;
  uint16_t port() const override { return port_; }  // of this run; the platform's own when stopped

  static constexpr int kMaxConnections = 4;    // Finder opens two or three at once
  static constexpr int kIdleTimeoutSec = 30;   // a silent connection is closed

 private:
  void acceptLoop();
  void serve(int fd);

  const uint16_t ownPort_;
  std::atomic<uint16_t> port_;
  std::atomic<bool> running_{false};
  std::atomic<int> listenFd_{-1};  // closed by stop() while the accept thread reads it
  std::thread acceptThread_;
  std::mutex mu_;
  std::vector<int> clients_;
  std::atomic<int> active_{0};
  hal::ServerHandler handler_;
};

}  // namespace pos::platform
