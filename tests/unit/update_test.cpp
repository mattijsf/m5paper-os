// Firmware update over Wi-Fi (task P7-3, D37): the package header and the server that takes an image (core/update).
#include <doctest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <sodium.h>

#include "core/remote.h"
#include "core/update.h"

using namespace pos;
using namespace pos::update;

namespace {

struct Keys {
  PublicKey pk{};
  uint8_t sk[crypto_sign_SECRETKEYBYTES];
  Keys() {
    REQUIRE(sodium_init() >= 0);
    crypto_sign_keypair(pk.data(), sk);
  }
};

// What tools/ota.py pack makes.
std::string package(const Keys& k, const std::string& image, const std::string& build = "abcdef012345") {
  std::string head(kMagic, 8);
  std::string b = build;
  b.resize(32, '\0');
  head += b;
  const uint32_t size = static_cast<uint32_t>(image.size());
  for (int i = 0; i < 4; ++i) head += static_cast<char>(size >> (8 * i));
  uint8_t hash[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(hash, reinterpret_cast<const uint8_t*>(image.data()), image.size());
  head.append(reinterpret_cast<const char*>(hash), sizeof hash);
  const uint64_t made = 1795000000;
  for (int i = 0; i < 8; ++i) head += static_cast<char>(made >> (8 * i));
  REQUIRE(head.size() == kSignedBytes);
  uint8_t sig[crypto_sign_BYTES];
  crypto_sign_detached(sig, nullptr, reinterpret_cast<const uint8_t*>(head.data()), head.size(), k.sk);
  head.append(reinterpret_cast<const char*>(sig), sizeof sig);
  REQUIRE(head.size() == kHeaderBytes);
  return head + image;
}

struct FakeUpdater : hal::Updater {
  uint32_t slot = 1 << 20;
  bool failBegin = false, failWrite = false, failFinish = false;
  std::string written;
  int begun = 0, finished = 0, aborted = 0;
  uint32_t slotSize() override { return slot; }
  bool begin(uint32_t, std::string& error) override {
    if (failBegin) {
      error = "busy";
      return false;
    }
    ++begun;
    written.clear();
    return true;
  }
  bool write(const uint8_t* data, size_t n) override {
    if (failWrite) return false;
    written.append(reinterpret_cast<const char*>(data), n);
    return true;
  }
  bool finish(std::string& error) override {
    if (failFinish) {
      error = "bad image";
      return false;
    }
    ++finished;
    return true;
  }
  void abort() override { ++aborted; }
  bool pendingVerify() override { return false; }
  void confirm() override {}
  bool rolledBack() override { return false; }
  std::string runningSlot() override { return "app0"; }
};

std::string image(size_t n) {
  std::string s(n, '\0');
  for (size_t i = 0; i < n; ++i) s[i] = static_cast<char>((i * 31 + 7) & 0xFF);
  return s;
}

}  // namespace

TEST_CASE("update: a package header is taken only when this device's key signed it") {
  Keys k, other;
  const std::string p = package(k, image(1000), "0123456789ab");
  Header h;
  CHECK(parseHeader(reinterpret_cast<const uint8_t*>(p.data()), kHeaderBytes, k.pk, h) == "");
  CHECK(h.build == "0123456789ab");
  CHECK(h.size == 1000);
  CHECK(h.made == 1795000000);
  CHECK(parseHeader(reinterpret_cast<const uint8_t*>(p.data()), kHeaderBytes, other.pk, h) == "Not signed with this device's key");
  CHECK(parseHeader(reinterpret_cast<const uint8_t*>(p.data()), kHeaderBytes - 1, k.pk, h) == "Not a PaperOS update package");
  std::string bad = p;
  bad[0] = 'X';
  CHECK(parseHeader(reinterpret_cast<const uint8_t*>(bad.data()), kHeaderBytes, k.pk, h) == "Not a PaperOS update package");
  // Any change to what was signed: the size, the hash, the build.
  for (const size_t at : {size_t(9), size_t(40), size_t(50), size_t(80)}) {
    std::string changed = p;
    changed[at] ^= 1;
    CHECK(parseHeader(reinterpret_cast<const uint8_t*>(changed.data()), kHeaderBytes, k.pk, h) == "Not signed with this device's key");
  }
  // A firmware built before a key existed takes nothing.
  CHECK_FALSE(keySet(PublicKey{}));
  CHECK(keySet(k.pk));
  CHECK(parseHeader(reinterpret_cast<const uint8_t*>(p.data()), kHeaderBytes, PublicKey{}, h).find("no update key") != std::string::npos);
  CHECK(parseHeader(reinterpret_cast<const uint8_t*>(package(k, "").data()), kHeaderBytes, k.pk, h) == "The package holds no image");
}

// Feeds a package in pieces of `piece` bytes, as a socket or the serial port gives them.
std::string feedAll(ImageWriter& w, const std::string& p, size_t piece = 5000) {
  for (size_t i = 0; i < p.size(); i += piece) {
    const std::string bad = w.feed(reinterpret_cast<const uint8_t*>(p.data()) + i, std::min(piece, p.size() - i));
    if (!bad.empty()) return bad;
  }
  return w.finish();
}

TEST_CASE("update: the writer writes a signed image whole and refuses everything else before or without making it boot") {
  Keys k, other;
  const std::string img = image(70000);
  const std::string good = package(k, img);
  FakeUpdater up;
  {  // signed by another key: refused after the header, nothing begun
    ImageWriter w(up, k.pk);
    CHECK(feedAll(w, package(other, img)) == "Not signed with this device's key");
    CHECK(up.begun == 0);
  }
  {  // not a package; longer than it says; too large for the slot
    ImageWriter a(up, k.pk);
    CHECK(feedAll(a, std::string(500, 'x')) == "Not a PaperOS update package");
    ImageWriter b(up, k.pk);
    CHECK(feedAll(b, good + "extra") == "The package is longer than it says");
    up.slot = 60000;
    ImageWriter c(up, k.pk);
    CHECK(feedAll(c, good).find("larger than the slot") != std::string::npos);
    up.slot = 1 << 20;
  }
  const int begunBefore = up.begun;
  {  // the image changed after it was signed: written, never made the one that boots
    std::string tampered = good;
    tampered[kHeaderBytes + 12345] ^= 0x40;
    ImageWriter w(up, k.pk);
    CHECK(feedAll(w, tampered) == "The image is not the one that was signed");
    CHECK(up.finished == 0);
  }
  {  // stopped halfway; a write refused; begin refused; the chip refuses the image
    ImageWriter a(up, k.pk);
    a.feed(reinterpret_cast<const uint8_t*>(good.data()), kHeaderBytes + 30000);
    CHECK(a.finish() == "The upload stopped after 30000 of 70000 bytes");
    up.failWrite = true;
    ImageWriter b(up, k.pk);
    CHECK(feedAll(b, good) == "Writing the image failed");
    up.failWrite = false;
    up.failBegin = true;
    ImageWriter c(up, k.pk);
    CHECK(feedAll(c, good) == "Cannot start the update: busy");
    up.failBegin = false;
    up.failFinish = true;
    ImageWriter d(up, k.pk);
    CHECK(feedAll(d, good) == "The image was refused: bad image");
    up.failFinish = false;
  }
  CHECK(up.finished == 0);
  CHECK(up.aborted >= up.begun - begunBefore - 1);  // what was begun was given up
  {  // the real thing, in odd pieces
    ImageWriter w(up, k.pk);
    CHECK(feedAll(w, good, 1001) == "");
    CHECK(up.written == img);
    CHECK(up.finished == 1);
    CHECK(w.header().build == "abcdef012345");
    CHECK(w.imageBytes() == 70000);
  }
}

TEST_CASE("update: the wireless sign-in takes a signature of this nonce with this key, nothing else") {
  Keys k, other;
  uint8_t nonce[kNonceBytes];
  for (size_t i = 0; i < kNonceBytes; ++i) nonce[i] = static_cast<uint8_t>(i * 7);
  std::string hex;
  const std::string hello = helloLine("abcdef012345", nonce, hex);
  CHECK(hello == "#hello paperos 1 abcdef012345 " + hex);
  CHECK(hex.size() == 64);
  auto signLine = [&](const uint8_t* sk, const std::string& nonceHex) {
    const std::string msg = "paperos-remote:" + nonceHex;
    uint8_t sig[crypto_sign_BYTES];
    crypto_sign_detached(sig, nullptr, reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), sk);
    static const char kHex[] = "0123456789abcdef";
    std::string line = ">auth ";
    for (uint8_t b : sig) {
      line += kHex[b >> 4];
      line += kHex[b & 15];
    }
    return line;
  };
  CHECK(authOk(hex, signLine(k.sk, hex), k.pk));
  CHECK(authOk(hex, signLine(k.sk, hex) + "\r", k.pk));      // a line from a terminal
  CHECK_FALSE(authOk(hex, signLine(other.sk, hex), k.pk));    // another key
  std::string old = hex;
  old[0] = old[0] == 'a' ? 'b' : 'a';
  CHECK_FALSE(authOk(hex, signLine(k.sk, old), k.pk));        // an old challenge (a recorded sign-in)
  CHECK_FALSE(authOk(hex, ">shot", k.pk));
  CHECK_FALSE(authOk(hex, ">auth zz", k.pk));
  CHECK_FALSE(authOk(hex, signLine(k.sk, hex), PublicKey{}));  // a firmware without a key signs nobody in
}

TEST_CASE("update: >ota parses its size") {
  RemoteCommand c = parseRemoteCommand(">ota 2591252");
  CHECK(c.kind == RemoteCommand::Ota);
  CHECK(c.size == 2591252);
  CHECK(parseRemoteCommand(">ota").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">ota -5").kind == RemoteCommand::None);
}

TEST_CASE("update: the key in the firmware source is a key or all zero") {
  const PublicKey& key = firmwareKey();
  CHECK(key.size() == 32);
}
