// Firmware update over Wi-Fi (task P7-3, D37): the package header and the server that takes an image (core/update).
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include <sodium.h>

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

hal::ServerRequest request(const std::string& method, const std::string& target, const std::string& body, size_t cutAt = std::string::npos) {
  hal::ServerRequest r;
  r.method = method;
  r.target = target;
  r.contentLength = static_cast<int64_t>(body.size());
  auto pos = std::make_shared<size_t>(0);
  const std::string data = cutAt == std::string::npos ? body : body.substr(0, cutAt);
  r.read = [data, pos](char* out, size_t n) -> long {
    const size_t take = std::min(n, std::min<size_t>(data.size() - *pos, 5000));  // in pieces, as a socket gives them
    std::memcpy(out, data.data() + *pos, take);
    *pos += take;
    return static_cast<long>(take);
  };
  return r;
}

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

TEST_CASE("update: the server writes a signed image whole and refuses everything else before or without making it boot") {
  Keys k, other;
  uint32_t now = 1000;
  const std::string img = image(70000);

  FakeUpdater up;
  Server server(up, k.pk, "runningbuild", [&] { return now; });
  hal::ServerRequest info = request("GET", "/", "");
  hal::ServerResponse r = server.handle(info);
  CHECK(r.status == 200);
  CHECK(r.body == "{\"paperos\":\"update\",\"build\":\"runningbuild\",\"slot\":\"app0\",\"max\":1048576}\n");
  hal::ServerRequest other404 = request("GET", "/update", "");
  CHECK(server.handle(other404).status == 404);

  // Signed by another key: refused after the header, nothing begun.
  hal::ServerRequest foreign = request("POST", "/update", package(other, img));
  r = server.handle(foreign);
  CHECK(r.status == 403);
  CHECK(up.begun == 0);
  CHECK(server.stats().last == "Not signed with this device's key");
  CHECK_FALSE(server.stats().busy);

  // Not a package at all, a package longer or shorter than it says, an image too large for the slot.
  hal::ServerRequest junk = request("POST", "/update", std::string(500, 'x'));
  CHECK(server.handle(junk).status == 400);
  hal::ServerRequest tiny = request("POST", "/update", "short");
  CHECK(server.handle(tiny).status == 400);
  hal::ServerRequest padded = request("POST", "/update", package(k, img) + "extra");
  CHECK(server.handle(padded).status == 400);
  up.slot = 60000;
  hal::ServerRequest large = request("POST", "/update", package(k, img));
  CHECK(server.handle(large).status == 413);
  up.slot = 1 << 20;
  CHECK(up.begun == 0);

  // The image changed after it was signed: written to the slot, never made the one that boots.
  std::string tampered = package(k, img);
  tampered[kHeaderBytes + 12345] ^= 0x40;
  hal::ServerRequest t = request("POST", "/update", tampered);
  r = server.handle(t);
  CHECK(r.status == 400);
  CHECK(r.body == "The image is not the one that was signed\n");
  CHECK(up.begun == 1);
  CHECK(up.aborted == 1);
  CHECK(up.finished == 0);

  // The upload stops halfway; the flash refuses a write; the chip refuses the image.
  const std::string good = package(k, img);
  hal::ServerRequest cut = request("POST", "/update", good, kHeaderBytes + 30000);
  r = server.handle(cut);
  CHECK(r.status == 400);
  CHECK(r.body.find("stopped after 30000 of 70000") != std::string::npos);
  CHECK(up.aborted == 2);
  up.failWrite = true;
  hal::ServerRequest w = request("POST", "/update", good);
  CHECK(server.handle(w).status == 500);
  up.failWrite = false;
  up.failBegin = true;
  hal::ServerRequest b = request("POST", "/update", good);
  CHECK(server.handle(b).status == 500);
  up.failBegin = false;
  up.failFinish = true;
  hal::ServerRequest f = request("POST", "/update", good);
  r = server.handle(f);
  CHECK(r.status == 500);
  CHECK(r.body == "The image was refused: bad image\n");
  up.failFinish = false;
  CHECK(up.finished == 0);
  CHECK_FALSE(server.stats().done);

  // The real thing.
  now = 5000;
  hal::ServerRequest ok = request("POST", "/update", good);
  r = server.handle(ok);
  CHECK(r.status == 200);
  CHECK(r.body == "ok: build abcdef012345 is in place, the device restarts\n");
  CHECK(up.written == img);
  CHECK(up.finished == 1);
  const Stats s = server.stats();
  CHECK(s.done);
  CHECK_FALSE(s.busy);
  CHECK(s.received == 70000);
  CHECK(s.size == 70000);
  CHECK(s.build == "abcdef012345");
  CHECK(server.lastRequestMs() == 5000);
  // Nothing more is taken until the restart.
  hal::ServerRequest again = request("POST", "/update", good);
  CHECK(server.handle(again).status == 409);
  CHECK(up.finished == 1);
}

TEST_CASE("update: the key in the firmware source is a key or all zero") {
  const PublicKey& key = firmwareKey();
  CHECK(key.size() == 32);
}
