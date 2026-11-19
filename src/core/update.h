// Firmware update (task P7-3, DECISIONS.md D37, D39): the package `make ota` sends, its check and its writing, and the
// handshake of wireless development (core/remote.h `>ota`, platform/posix/remote_server). Unit-tested.
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
#include <memory>
#include <string>

#include "hal/update.h"

namespace pos::update {

// The port of wireless development (remote control and updates), device and simulator: its own, not the file server's 80
// (a service of its own, and the tools then do not depend on what a router thinks of port 80: D37, D39).
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

// Writes a package as it arrives (any chunking): the header is checked before a byte of the image is written, the
// image's SHA-256 before it is made the one that boots. Used by the remote's `>ota` command (core/remote.h).
class ImageWriter {
 public:
  ImageWriter(hal::Updater& updater, PublicKey key);
  ~ImageWriter();
  // "" while all is well; else why not (everything begun is given up then; later bytes are refused).
  std::string feed(const uint8_t* data, size_t n);
  // After the last byte: "" when the image is in place and boots at the next restart; else why not.
  std::string finish();
  void abort();
  bool headerRead() const { return headerDone_; }
  const Header& header() const { return header_; }
  uint32_t imageBytes() const { return got_; }

 private:
  hal::Updater& updater_;
  PublicKey key_;
  uint8_t head_[kHeaderBytes]{};
  size_t headGot_ = 0;
  bool headerDone_ = false, begun_ = false, failed_ = false;
  Header header_;
  uint32_t got_ = 0;
  struct Hash;  // crypto_hash_sha256_state, kept out of this header
  std::unique_ptr<Hash> hash_;
};

// Remote control over Wi-Fi (D39): a new connection gets "#hello paperos 1 <build> <nonce hex>", and must answer
// ">auth <hex of the Ed25519 signature of 'paperos-remote:' + nonce hex>" with the update key before anything else.
constexpr size_t kNonceBytes = 32;
std::string helloLine(const std::string& build, const uint8_t (&nonce)[kNonceBytes], std::string& nonceHex);
bool authOk(const std::string& nonceHex, const std::string& line, const PublicKey& key);

}  // namespace pos::update
