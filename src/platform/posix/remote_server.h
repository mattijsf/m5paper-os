// Wireless development (DECISIONS.md D39): the remote's commands (core/remote.h) over TCP on the home Wi-Fi, while
// Settings > Developer > Wireless development is on. One connection at a time, and only after it has signed a fresh
// challenge with the update key (core/update.h helloLine / authOk): the Mac that builds the firmware; or with a key from the
// companion pairing (core/remote_phone.h): the paired phone, screen and touch only. Nobody else.
// Polled from the main loop on the UI thread (the Remote is not thread-safe); sockets are non-blocking. The same code on
// the device (lwIP) and in the simulator.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "core/os.h"
#include "core/remote.h"

namespace pos::platform {

class RemoteServer : public RemoteHost {
 public:
  struct Hooks {
    std::function<void(const hal::TouchEvent&)> inject;
    std::function<bool(const std::string&, const std::string&)> option;  // ">set key value"
    std::function<void()> reboot;
  };
  // `needWifi`: listen only while the net service is online, and bring it up (the device); false: always (the simulator).
  RemoteServer(Os& os, hal::Clock& clock, uint16_t port, bool needWifi, Hooks hooks);
  ~RemoteServer() override;
  // Each main-loop pass: follows the setting, keeps Wi-Fi up, accepts, signs in, feeds the remote, sends what it says.
  void poll();
  bool connected() const { return client_ >= 0 && authed_; }

  // RemoteHost
  void inject(const hal::TouchEvent& ev) override { hooks_.inject(ev); }
  void reply(const void* data, size_t n) override;
  size_t writable() override { return client_ >= 0 ? 32768 : static_cast<size_t>(-1); }
  void reboot() override { hooks_.reboot(); }
  bool option(const std::string& key, const std::string& value) override { return hooks_.option && hooks_.option(key, value); }

 private:
  void startListening();
  void stopListening();
  void closeClient(const char* why);
  void accept();
  void read();

  Os& os_;
  hal::Clock& clock_;
  uint16_t port_;
  bool needWifi_;
  Hooks hooks_;
  int listen_ = -1, client_ = -1;
  bool authed_ = false;
  bool phone_ = false;  // the client signed in as the paired phone (core/remote_phone.h)
  std::string nonceHex_, line_;
  uint32_t connectedMs_ = 0, lastKeepMs_ = 0, lastJoinMs_ = 0;
  bool joinTried_ = false;
  std::unique_ptr<Remote> remote_;  // one per signed-in connection
};

}  // namespace pos::platform
