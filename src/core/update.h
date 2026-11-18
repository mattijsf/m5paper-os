// Firmware update over Wi-Fi (task P7-3, DECISIONS.md D37): the package `make ota` sends and the server that takes it
// while Settings > About > Update over Wi-Fi is open. Shared by the device and the simulator; unit-tested.
//
// Package = a 148-byte header, then the firmware image (firmware.bin) as it is:
//   0   8  magic "POSOTA1\n"
//   8  32  build: what the sender calls this build (hal::buildId() of the new firmware and a date), zero padded
//  40   4  size of the image in bytes, little endian
//  44  32  SHA-256 of the image
//  76   8  made: unix seconds when it was signed, little endian
//  84  64  Ed25519 signature over bytes 0..83, by the key whose public half is compiled into the firmware
// The header is checked before a byte is written; the image's hash is checked before it is made the one that boots.
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "hal/http_server.h"
#include "hal/update.h"

namespace pos::update {

// The port the update page listens on, device and simulator: its own, not the file server's 80 (a service of its own,
// and `make ota` then does not depend on what a router thinks of port 80: DECISIONS.md D37).
constexpr uint16_t kPort = 8266;
constexpr size_t kHeaderBytes = 148, kSignedBytes = 84, kKeyBytes = 32, kHashBytes = 32, kSigBytes = 64;
constexpr const char kMagic[] = "POSOTA1\n";
using PublicKey = std::array<uint8_t, kKeyBytes>;

struct Header {
  std::string build;
  uint32_t size = 0;
  std::array<uint8_t, kHashBytes> sha256{};
  int64_t made = 0;
};
// Reads and checks a package header. "" when it is a package signed by `key`; else why not, for the sender.
std::string parseHeader(const uint8_t* bytes, size_t n, const PublicKey& key, Header& out);
// False for the all-zero key of a firmware built before a signing key existed (tools/ota.py keygen).
bool keySet(const PublicKey& key);
// The key compiled into this firmware (src/core/update_key.h).
const PublicKey& firmwareKey();

struct Stats {
  bool busy = false;       // an image is arriving
  uint32_t received = 0;   // bytes of it so far
  uint32_t size = 0;       // of the image arriving, or of the last one
  bool done = false;       // an image is in place: restart to run it
  std::string build;       // its build
  std::string last;        // the last outcome in words: "Not signed with this device's key", ...
};

// Answers the update page's requests:
//   GET  /         one line of JSON: who this is, which build runs, how large an image may be (the sender finds the device by it)
//   POST /update   the package; 200 when the image is in place, else 4xx / 5xx with the reason as text
// One upload at a time. Handler threads call handle(); the UI thread reads stats().
class Server {
 public:
  Server(hal::Updater& updater, PublicKey key, std::string runningBuild, std::function<uint32_t()> millis);
  hal::ServerResponse handle(hal::ServerRequest& req);
  Stats stats() const;
  uint32_t lastRequestMs() const { return lastRequest_.load(); }

 private:
  hal::ServerResponse receive(hal::ServerRequest& req);
  void setLast(const std::string& text);

  hal::Updater& updater_;
  PublicKey key_;
  std::string running_;
  std::function<uint32_t()> millis_;
  mutable std::mutex mu_;
  Stats stats_;
  std::atomic<uint32_t> lastRequest_{0};
};

}  // namespace pos::update
