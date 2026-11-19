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

struct ImageWriter::Hash {
  crypto_hash_sha256_state state;
};

ImageWriter::ImageWriter(hal::Updater& updater, PublicKey key) : updater_(updater), key_(key) {}
ImageWriter::~ImageWriter() { abort(); }

std::string ImageWriter::feed(const uint8_t* data, size_t n) {
  if (failed_) return "The update was given up";
  while (n > 0 && !headerDone_) {
    const size_t take = std::min(n, kHeaderBytes - headGot_);
    std::memcpy(head_ + headGot_, data, take);
    headGot_ += take;
    data += take;
    n -= take;
    if (headGot_ < kHeaderBytes) return "";
    const std::string bad = parseHeader(head_, kHeaderBytes, key_, header_);
    if (!bad.empty()) {
      failed_ = true;
      return bad;
    }
    headerDone_ = true;
    const uint32_t max = updater_.slotSize();
    if (max == 0) {
      failed_ = true;
      return "This device cannot be updated";
    }
    if (header_.size > max) {
      failed_ = true;
      return "The image is larger than the slot (" + std::to_string(header_.size) + " of " + std::to_string(max) + " bytes)";
    }
    std::string error;
    if (!updater_.begin(header_.size, error)) {
      failed_ = true;
      return "Cannot start the update: " + error;
    }
    begun_ = true;
    hash_ = std::make_unique<Hash>();
    crypto_hash_sha256_init(&hash_->state);
  }
  if (n == 0) return "";
  if (got_ + n > header_.size) {
    abort();
    return "The package is longer than it says";
  }
  crypto_hash_sha256_update(&hash_->state, data, n);
  if (!updater_.write(data, n)) {
    abort();
    return "Writing the image failed";
  }
  got_ += static_cast<uint32_t>(n);
  return "";
}

std::string ImageWriter::finish() {
  if (failed_) return "The update was given up";
  if (!headerDone_ || got_ != header_.size) {
    abort();
    return "The upload stopped after " + std::to_string(got_) + " of " + std::to_string(header_.size) + " bytes";
  }
  uint8_t digest[kHashBytes];
  crypto_hash_sha256_final(&hash_->state, digest);
  if (sodium_memcmp(digest, header_.sha256.data(), kHashBytes) != 0) {
    abort();
    return "The image is not the one that was signed";
  }
  std::string error;
  begun_ = false;
  if (!updater_.finish(error)) {
    failed_ = true;
    return "The image was refused: " + error;
  }
  return "";
}

void ImageWriter::abort() {
  failed_ = true;
  if (begun_) updater_.abort();
  begun_ = false;
}

std::string helloLine(const std::string& build, const uint8_t (&nonce)[kNonceBytes], std::string& nonceHex) {
  static const char kHex[] = "0123456789abcdef";
  nonceHex.clear();
  for (const uint8_t b : nonce) {
    nonceHex += kHex[b >> 4];
    nonceHex += kHex[b & 15];
  }
  return "#hello paperos 1 " + build + " " + nonceHex;
}

bool authOk(const std::string& nonceHex, const std::string& line, const PublicKey& key) {
  static const char kWord[] = ">auth ";
  if (!keySet(key) || nonceHex.size() != 2 * kNonceBytes || line.compare(0, sizeof kWord - 1, kWord) != 0) return false;
  std::string hex = line.substr(sizeof kWord - 1);
  while (!hex.empty() && (hex.back() == '\r' || hex.back() == ' ')) hex.pop_back();
  if (hex.size() != 2 * kSigBytes) return false;
  uint8_t sig[kSigBytes];
  for (size_t i = 0; i < kSigBytes; ++i) {
    auto nib = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    const int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    sig[i] = static_cast<uint8_t>(hi << 4 | lo);
  }
  const std::string msg = "paperos-remote:" + nonceHex;
  return sodium_init() >= 0 &&
         crypto_sign_verify_detached(sig, reinterpret_cast<const uint8_t*>(msg.data()), msg.size(), key.data()) == 0;
}

}  // namespace pos::update
