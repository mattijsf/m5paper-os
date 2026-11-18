#include "core/update.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <sodium.h>

#include "core/log.h"
#include "core/update_key.h"

namespace pos::update {

namespace {

uint32_t le32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24; }

hal::ServerResponse text(int status, const std::string& body) {
  hal::ServerResponse r;
  r.status = status;
  r.headers = {{"Content-Type", "text/plain; charset=utf-8"}, {"Connection", "close"}};
  r.body = body + "\n";
  return r;
}

// Reads exactly n bytes of the body; false if it ends or fails before that.
bool readAll(hal::ServerRequest& req, uint8_t* out, size_t n) {
  size_t got = 0;
  while (got < n) {
    const long r = req.read(reinterpret_cast<char*>(out) + got, n - got);
    if (r <= 0) return false;
    got += static_cast<size_t>(r);
  }
  return true;
}

}  // namespace

bool keySet(const PublicKey& key) {
  for (const uint8_t b : key)
    if (b != 0) return true;
  return false;
}

const PublicKey& firmwareKey() {
  static const PublicKey key = kUpdateKey;
  return key;
}

std::string parseHeader(const uint8_t* b, size_t n, const PublicKey& key, Header& out) {
  if (n < kHeaderBytes || std::memcmp(b, kMagic, 8) != 0) return "Not a PaperOS update package";
  if (!keySet(key)) return "This firmware has no update key: flash once over USB after `make ota-key`";
  if (sodium_init() < 0 || crypto_sign_verify_detached(b + kSignedBytes, b, kSignedBytes, key.data()) != 0)
    return "Not signed with this device's key";
  out = Header{};
  size_t len = 0;
  while (len < 32 && b[8 + len] != 0) ++len;
  for (size_t i = 0; i < len; ++i) {  // printable ASCII only: it goes on the panel and into the log
    const char c = static_cast<char>(b[8 + i]);
    out.build += (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  out.size = le32(b + 40);
  std::memcpy(out.sha256.data(), b + 44, kHashBytes);
  out.made = static_cast<int64_t>(static_cast<uint64_t>(le32(b + 76)) | static_cast<uint64_t>(le32(b + 80)) << 32);
  if (out.size == 0) return "The package holds no image";
  return "";
}

Server::Server(hal::Updater& updater, PublicKey key, std::string runningBuild, std::function<uint32_t()> millis)
    : updater_(updater), key_(key), running_(std::move(runningBuild)), millis_(std::move(millis)) {
  lastRequest_ = millis_();
}

Stats Server::stats() const {
  std::lock_guard<std::mutex> lock(mu_);
  return stats_;
}

void Server::setLast(const std::string& text) {
  std::lock_guard<std::mutex> lock(mu_);
  stats_.busy = false;
  stats_.last = text;
}

hal::ServerResponse Server::handle(hal::ServerRequest& req) {
  lastRequest_ = millis_();
  const std::string path = req.target.substr(0, req.target.find('?'));
  if (req.method == "GET" && path == "/") {
    hal::ServerResponse r;
    r.headers = {{"Content-Type", "application/json"}, {"Connection", "close"}};
    r.body = "{\"paperos\":\"update\",\"build\":\"" + running_ + "\",\"slot\":\"" + updater_.runningSlot() + "\",\"max\":" +
             std::to_string(updater_.slotSize()) + "}\n";
    return r;
  }
  if (req.method == "POST" && path == "/update") {
    hal::ServerResponse r = receive(req);
    lastRequest_ = millis_();
    return r;
  }
  return text(404, "PaperOS update: POST /update");
}

hal::ServerResponse Server::receive(hal::ServerRequest& req) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (stats_.busy) return text(409, "An update is arriving already");
    if (stats_.done) return text(409, "An update is in place: the device restarts");
    stats_.busy = true;
    stats_.received = 0;
    stats_.size = 0;
    stats_.last.clear();
  }
  auto fail = [this](int status, const std::string& why) {
    setLast(why);
    POS_LOG("update", "refused: %s", why.c_str());
    return text(status, why);
  };
  uint8_t head[kHeaderBytes];
  if (!readAll(req, head, sizeof head)) return fail(400, "Not a PaperOS update package");
  Header h;
  const std::string bad = parseHeader(head, sizeof head, key_, h);
  if (!bad.empty()) return fail(bad.find("key") != std::string::npos ? 403 : 400, bad);
  const uint32_t max = updater_.slotSize();
  if (max == 0) return fail(501, "This device cannot be updated over Wi-Fi");
  if (h.size > max) return fail(413, "The image is larger than the slot (" + std::to_string(h.size) + " of " + std::to_string(max) + " bytes)");
  if (req.contentLength >= 0 && req.contentLength != static_cast<int64_t>(kHeaderBytes) + h.size) return fail(400, "The package is not as long as it says");
  std::string error;
  if (!updater_.begin(h.size, error)) return fail(500, "Cannot start the update: " + error);
  {
    std::lock_guard<std::mutex> lock(mu_);
    stats_.size = h.size;
    stats_.build = h.build;
  }
  POS_LOG("update", "receiving build %s, %u bytes", h.build.c_str(), static_cast<unsigned>(h.size));
  crypto_hash_sha256_state hash;
  crypto_hash_sha256_init(&hash);
  // One flash sector at a time, from a buffer that is not in PSRAM (one upload at a time: `busy`): a write to the
  // internal flash stops the cache, and with it PSRAM, for as long as it takes.
  static uint8_t buf[4096];
  uint32_t got = 0, slowest = 0;
  const uint32_t t0 = millis_();
  while (got < h.size) {
    const size_t want = std::min<size_t>(sizeof buf, h.size - got);
    const long n = req.read(reinterpret_cast<char*>(buf), want);
    if (n <= 0) {
      updater_.abort();
      return fail(400, "The upload stopped after " + std::to_string(got) + " of " + std::to_string(h.size) + " bytes");
    }
    crypto_hash_sha256_update(&hash, buf, static_cast<size_t>(n));
    const uint32_t w0 = millis_();
    if (!updater_.write(buf, static_cast<size_t>(n))) {
      updater_.abort();
      return fail(500, "Writing the image failed");
    }
    slowest = std::max(slowest, millis_() - w0);
    got += static_cast<uint32_t>(n);
    lastRequest_ = millis_();
    std::lock_guard<std::mutex> lock(mu_);
    stats_.received = got;
  }
  uint8_t digest[kHashBytes];
  crypto_hash_sha256_final(&hash, digest);
  if (sodium_memcmp(digest, h.sha256.data(), kHashBytes) != 0) {
    updater_.abort();
    return fail(400, "The image is not the one that was signed");
  }
  if (!updater_.finish(error)) return fail(500, "The image was refused: " + error);
  {
    std::lock_guard<std::mutex> lock(mu_);
    stats_.busy = false;
    stats_.done = true;
    stats_.last = "Update received";
  }
  POS_LOG("update", "build %s is in place after %u ms (slowest write %u ms): restarting", h.build.c_str(), static_cast<unsigned>(millis_() - t0), static_cast<unsigned>(slowest));
  return text(200, "ok: build " + h.build + " is in place, the device restarts");
}

}  // namespace pos::update
