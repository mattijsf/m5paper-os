#include "platform/posix/remote_server.h"

// Order matters on the device: Arduino first (see http_server.cpp).
#if defined(ESP_PLATFORM)
#include <Arduino.h>
#endif
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "core/companion_crypto.h"
#include "core/log.h"
#include "core/remote_phone.h"
#include "core/update.h"
#include "hal/system.h"

namespace pos::platform {

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif
constexpr uint32_t kSignInMs = 10000;     // a connection that has not signed in by then is closed
constexpr uint32_t kJoinRetryMs = 30000;  // Wi-Fi down: try to join again this often

void nonBlocking(int fd) { ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

}  // namespace

RemoteServer::RemoteServer(Os& os, hal::Clock& clock, uint16_t port, bool needWifi, Hooks hooks)
    : os_(os), clock_(clock), port_(port), needWifi_(needWifi), hooks_(std::move(hooks)) {}

RemoteServer::~RemoteServer() {
  closeClient(nullptr);
  stopListening();
}

void RemoteServer::poll() {
  // A Remote outlives its connection until it has nothing left to do: a tap's finger is lifted ~80 ms after the "#ok",
  // and the tool hangs up at once (2026-11-19: taps over Wi-Fi did nothing, the finger stayed down).
  if (client_ < 0 && remote_ && !remote_->busy()) remote_.reset();
  WirelessStatus& st = wirelessStatus();
  st.on = os_.config().developer.wireless;
  st.port = port_;
  if (!st.on) {
    if (client_ >= 0) closeClient("wireless development switched off");
    stopListening();
    st.listening = st.client = false;
    st.address.clear();
    st.phone = false;
    return;
  }
  const uint32_t now = clock_.millis();
  if (needWifi_) {
    NetService& net = os_.net();
    if (now - lastKeepMs_ >= 1000) {
      lastKeepMs_ = now;
      net.keepAlive();  // Wi-Fi stays up while this is on (and the device does not light-sleep with Wi-Fi up)
    }
    if (!net.online()) {
      if (client_ >= 0) closeClient("Wi-Fi went down");
      stopListening();
      st.listening = false;
      st.address.clear();
      if (!joinTried_ || now - lastJoinMs_ >= kJoinRetryMs) {
        joinTried_ = true;
        lastJoinMs_ = now;
        NetService* n = &net;
        os_.worker().post([n] { n->ensureConnected(); });
      }
      return;
    }
    st.address = net.link().ip;
  } else {
    st.address = "localhost";
  }
  if (listen_ < 0) startListening();
  st.listening = listen_ >= 0;
  if (st.listening && needWifi_) remotephone::announce(os_, st.address, port_, now);  // where the paired phone finds it
  accept();
  read();
  // A fresh reading: `now` is from before accept(), and a connection accepted since started later than it (now -
  // connectedMs_ then wrapped around and closed every such connection at once: 2026-11-19, found with the log below).
  if (client_ >= 0 && !authed_ && clock_.millis() - connectedMs_ > kSignInMs) closeClient("no sign-in");
  if (remote_ && (connected() || remote_->busy())) remote_->tick();
  st.client = connected();
  st.phone = st.client && phone_;
}

void RemoteServer::startListening() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return;
  int yes = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port_);
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || ::listen(fd, 2) != 0) {
    POS_LOG("remote", "wireless: cannot listen on port %u (errno %d)", static_cast<unsigned>(port_), errno);
    ::close(fd);
    return;
  }
  nonBlocking(fd);
  listen_ = fd;
  POS_LOG("remote", "wireless development: listening on port %u", static_cast<unsigned>(port_));
}

void RemoteServer::stopListening() {
  if (listen_ < 0) return;
  ::close(listen_);
  listen_ = -1;
}

void RemoteServer::closeClient(const char* why) {
  if (client_ < 0) return;
  POS_LOG("remote", "wireless: t=%u closing socket %d (%s)", static_cast<unsigned>(clock_.millis()), client_, why ? why : "-");
  ::close(client_);
  client_ = -1;
  authed_ = false;
  phone_ = false;
  line_.clear();  // (the Remote goes at the next poll: this may run from inside one of its replies)
}

void RemoteServer::accept() {
  if (listen_ < 0) return;
  const int fd = ::accept(listen_, nullptr, nullptr);
  if (fd < 0) return;
  if (client_ >= 0) closeClient("a new connection");  // the newest wins: a Mac that went away must not block the next
  nonBlocking(fd);
  int one = 1;
  ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#if defined(SO_NOSIGPIPE)
  ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  client_ = fd;
  authed_ = false;
  connectedMs_ = clock_.millis();
  sockaddr_in peer{};
  socklen_t plen = sizeof peer;
  ::getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &plen);
  POS_LOG("remote", "wireless: t=%u connection on socket %d from port %u", static_cast<unsigned>(connectedMs_), fd,
          static_cast<unsigned>(ntohs(peer.sin_port)));
  uint8_t nonce[update::kNonceBytes];
  companion::randomBytes(nonce, sizeof nonce);
  const std::string hello = update::helloLine(hal::buildId(), nonce, nonceHex_) + "\n";
  reply(hello.data(), hello.size());
}

void RemoteServer::read() {
  if (client_ < 0) return;
  static uint8_t buf[4096];  // one connection, the UI thread: not on the stack
  for (int rounds = 0; rounds < 16 && client_ >= 0; ++rounds) {
    const ssize_t n = ::recv(client_, buf, sizeof buf, 0);
    if (n == 0) {
      closeClient("closed by the other side");
      return;
    }
    if (n < 0) {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        POS_LOG("remote", "wireless: read on socket %d failed, errno %d", client_, errno);
        closeClient("read failed");
      }
      return;
    }
    if (authed_) {
      remote_->feed(buf, static_cast<size_t>(n));
      continue;
    }
    for (ssize_t i = 0; i < n; ++i) {  // the sign-in line
      const char ch = static_cast<char>(buf[i]);
      if (ch != '\n') {
        if (line_.size() < 300) line_ += ch;
        continue;
      }
      // The Mac signs with the update key; the paired phone answers with a key from the pairing (core/remote_phone.h).
      const bool phone = remotephone::isPhoneSignIn(line_);
      if (phone ? !remotephone::pairedAuthOk(nonceHex_, line_) : !update::authOk(nonceHex_, line_, update::firmwareKey())) {
        static const char kNo[] = "#err not signed with this device's key\n";
        static const char kNoPhone[] = "#err not the paired phone\n";
        if (phone) reply(kNoPhone, sizeof kNoPhone - 1);
        else reply(kNo, sizeof kNo - 1);
        closeClient(phone ? "not the paired phone" : "not signed with this device's key");
        return;
      }
      authed_ = true;
      phone_ = phone;
      line_.clear();
      if (remote_) remote_->tick();  // what the last connection left (a tap's finger) goes out first
      POS_LOG("remote", "wireless: t=%u socket %d signed in", static_cast<unsigned>(clock_.millis()), client_);
      remote_ = std::make_unique<Remote>(os_, clock_, *this, phone);
      static const char kOk[] = "#ok\n";
      reply(kOk, sizeof kOk - 1);
      POS_LOG("remote", "wireless: %s signed in", phone ? "the phone" : "a Mac");
      if (i + 1 < n) remote_->feed(buf + i + 1, static_cast<size_t>(n - i - 1));  // what came right behind the sign-in
      break;
    }
  }
}

void RemoteServer::reply(const void* data, size_t n) {
  const uint8_t* p = static_cast<const uint8_t*>(data);
  const uint32_t t0 = clock_.millis();
  while (n > 0 && client_ >= 0) {
    const ssize_t w = ::send(client_, p, n, kSendFlags);
    if (w > 0) {
      p += w;
      n -= static_cast<size_t>(w);
      continue;
    }
    if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      closeClient("write failed");
      return;
    }
    if (clock_.millis() - t0 > 3000) {
      closeClient("the Mac stopped reading");
      return;
    }
    fd_set set;
    FD_ZERO(&set);
    FD_SET(client_, &set);
    timeval tv{0, 20 * 1000};
    ::select(client_ + 1, nullptr, &set, nullptr, &tv);
  }
}

}  // namespace pos::platform
