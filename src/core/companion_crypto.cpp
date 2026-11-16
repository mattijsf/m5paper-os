#include "core/companion_crypto.h"

#include <cstdio>
#include <cstring>

#include <sodium.h>

namespace pos::companion {

namespace {

uint32_t gTestSeed = 0;
uint32_t gTestIndex = 0;

const char kSalt[] = "PaperOS companion v1";

void putBe64(uint8_t* p, uint64_t v) {
  for (int i = 7; i >= 0; --i) {
    p[i] = static_cast<uint8_t>(v & 0xff);
    v >>= 8;
  }
}
uint64_t getBe64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
  return v;
}

bool expand(uint8_t* out, size_t n, const uint8_t prk[crypto_kdf_hkdf_sha256_KEYBYTES], const char* label, const uint8_t* transcript) {
  uint8_t info[32 + 72];
  const size_t l = std::strlen(label);
  std::memcpy(info, label, l);
  std::memcpy(info + l, transcript, 72);
  return crypto_kdf_hkdf_sha256_expand(out, n, reinterpret_cast<const char*>(info), l + 72, prk) == 0;
}

}  // namespace

bool cryptoInit() {
  static int state = 0;  // 0 not tried, 1 ok, -1 failed
  if (state == 0) state = sodium_init() >= 0 ? 1 : -1;
  return state == 1;
}

void setTestRandom(uint32_t seed) {
  gTestSeed = seed;
  gTestIndex = 0;
}

void randomBytes(uint8_t* out, size_t n) {
  cryptoInit();
  if (gTestSeed == 0) {
    randombytes_buf(out, n);
    return;
  }
  const char prefix[] = "paperos-test-random";
  while (n > 0) {
    uint8_t in[sizeof prefix - 1 + 8];
    std::memcpy(in, prefix, sizeof prefix - 1);
    uint8_t* p = in + sizeof prefix - 1;
    const uint32_t s = gTestSeed, i = gTestIndex++;
    p[0] = s >> 24, p[1] = s >> 16, p[2] = s >> 8, p[3] = s;
    p[4] = i >> 24, p[5] = i >> 16, p[6] = i >> 8, p[7] = i;
    uint8_t h[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(h, in, sizeof in);
    const size_t take = n < sizeof h ? n : sizeof h;
    std::memcpy(out, h, take);
    out += take;
    n -= take;
  }
}

KeyPair makeKeyPair() {
  KeyPair kp;
  randomBytes(kp.sk.data(), kKeyBytes);
  publicKeyOf(kp.sk, kp.pk);
  return kp;
}

bool publicKeyOf(const Key& sk, Key& pk) {
  cryptoInit();
  return crypto_scalarmult_curve25519_base(pk.data(), sk.data()) == 0;
}

bool deriveSession(const Key& ownSk, const Key& peerPk, const Pid& pid, const Key& paperPk, const Key& phonePk, Session& out) {
  if (!cryptoInit()) return false;
  uint8_t ss[kKeyBytes];
  if (crypto_scalarmult_curve25519(ss, ownSk.data(), peerPk.data()) != 0) return false;  // also refuses an all-zero result
  uint8_t prk[crypto_kdf_hkdf_sha256_KEYBYTES];
  crypto_kdf_hkdf_sha256_extract(prk, reinterpret_cast<const uint8_t*>(kSalt), sizeof kSalt - 1, ss, sizeof ss);
  sodium_memzero(ss, sizeof ss);
  uint8_t transcript[72];
  std::memcpy(transcript, pid.data(), kPidBytes);
  std::memcpy(transcript + 8, paperPk.data(), kKeyBytes);
  std::memcpy(transcript + 40, phonePk.data(), kKeyBytes);
  uint8_t code[4];
  const bool ok = expand(out.p2d.data(), kKeyBytes, prk, "PaperOS p2d", transcript) &&
                  expand(out.d2p.data(), kKeyBytes, prk, "PaperOS d2p", transcript) && expand(code, 4, prk, "PaperOS code", transcript);
  sodium_memzero(prk, sizeof prk);
  out.code = ((uint32_t(code[0]) << 24) | (uint32_t(code[1]) << 16) | (uint32_t(code[2]) << 8) | code[3]) % 1000000u;
  return ok;
}

std::string codeText(uint32_t code) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "%03u %03u", static_cast<unsigned>(code / 1000 % 1000), static_cast<unsigned>(code % 1000));
  return buf;
}

std::string b64(const uint8_t* data, size_t n) {
  cryptoInit();
  std::string out(sodium_base64_ENCODED_LEN(n, sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
  sodium_bin2base64(&out[0], out.size(), data, n, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
  out.resize(std::strlen(out.c_str()));
  return out;
}

bool unb64(const std::string& text, std::vector<uint8_t>& out) {
  cryptoInit();
  for (char c : text)
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  out.assign(text.size() * 3 / 4 + 3, 0);
  size_t len = 0;
  const char* end = nullptr;
  if (sodium_base642bin(out.data(), out.size(), text.data(), text.size(), nullptr, &len, &end, sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0 ||
      end != text.data() + text.size())
    return false;
  out.resize(len);
  return true;
}

std::string seal(const Key& key, char dir, const Pid& pid, uint64_t counter, const std::string& plain, const uint8_t* nonce) {
  if (plain.size() > kMaxPlain || !cryptoInit()) return "";
  std::vector<uint8_t> env(kHeaderBytes + plain.size() + kTagBytes);
  env[0] = 1;
  env[1] = static_cast<uint8_t>(dir);
  std::memcpy(&env[2], pid.data(), kPidBytes);
  putBe64(&env[10], counter);
  if (nonce)
    std::memcpy(&env[18], nonce, kNonceBytes);
  else
    randomBytes(&env[18], kNonceBytes);
  unsigned long long clen = 0;
  crypto_aead_chacha20poly1305_ietf_encrypt(&env[kHeaderBytes], &clen, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), env.data(), 18,
                                            nullptr, &env[18], key.data());
  env.resize(kHeaderBytes + clen);
  return b64(env.data(), env.size());
}

const char* openErrorText(OpenError e) {
  switch (e) {
    case OpenError::None: return "ok";
    case OpenError::Size: return "size";
    case OpenError::Format: return "format";
    case OpenError::Version: return "version";
    case OpenError::Dir: return "dir";
    case OpenError::Pid: return "pid";
    case OpenError::Auth: return "auth";
  }
  return "?";
}

OpenError open(const Key& key, char dir, const Pid& pid, const std::string& env, Opened& out) {
  if (env.size() > kMaxEnvChars) return OpenError::Size;
  std::vector<uint8_t> b;
  if (!unb64(env, b)) return OpenError::Format;
  if (b.size() < kHeaderBytes + kTagBytes || b.size() > kHeaderBytes + kMaxPlain + kTagBytes) return OpenError::Size;
  if (b[0] != 1) return OpenError::Version;
  if (b[1] != static_cast<uint8_t>(dir)) return OpenError::Dir;
  if (std::memcmp(&b[2], pid.data(), kPidBytes) != 0) return OpenError::Pid;
  if (!cryptoInit()) return OpenError::Auth;
  std::string plain(b.size() - kHeaderBytes - kTagBytes, '\0');
  unsigned long long mlen = 0;
  if (crypto_aead_chacha20poly1305_ietf_decrypt(reinterpret_cast<uint8_t*>(&plain[0]), &mlen, nullptr, &b[kHeaderBytes], b.size() - kHeaderBytes,
                                                b.data(), 18, &b[18], key.data()) != 0)
    return OpenError::Auth;
  plain.resize(mlen);
  out.counter = getBe64(&b[10]);
  out.plain = std::move(plain);
  return OpenError::None;
}

std::string sealHello(const Key& p2d, const Pid& pid, const Key& phonePk, const std::string& plain, const uint8_t* nonce) {
  if (plain.size() > kMaxPlain || !cryptoInit()) return "";
  std::vector<uint8_t> env(kHelloHeaderBytes + plain.size() + kTagBytes);
  env[0] = 1;
  env[1] = static_cast<uint8_t>(kDirHello);
  std::memcpy(&env[2], pid.data(), kPidBytes);
  std::memcpy(&env[10], phonePk.data(), kKeyBytes);
  if (nonce)
    std::memcpy(&env[42], nonce, kNonceBytes);
  else
    randomBytes(&env[42], kNonceBytes);
  unsigned long long clen = 0;
  crypto_aead_chacha20poly1305_ietf_encrypt(&env[kHelloHeaderBytes], &clen, reinterpret_cast<const uint8_t*>(plain.data()), plain.size(), env.data(),
                                            42, nullptr, &env[42], p2d.data());
  env.resize(kHelloHeaderBytes + clen);
  return b64(env.data(), env.size());
}

OpenError openHello(const Key& paperSk, const Key& paperPk, const Pid& pid, const std::string& env, Hello& out) {
  if (env.size() > kMaxHelloChars) return OpenError::Size;
  std::vector<uint8_t> b;
  if (!unb64(env, b)) return OpenError::Format;
  if (b.size() < kHelloHeaderBytes + kTagBytes || b.size() > kHelloHeaderBytes + kMaxPlain + kTagBytes) return OpenError::Size;
  if (b[0] != 1) return OpenError::Version;
  if (b[1] != static_cast<uint8_t>(kDirHello)) return OpenError::Dir;
  if (std::memcmp(&b[2], pid.data(), kPidBytes) != 0) return OpenError::Pid;
  std::memcpy(out.phonePk.data(), &b[10], kKeyBytes);
  if (!deriveSession(paperSk, out.phonePk, pid, paperPk, out.phonePk, out.session)) return OpenError::Auth;
  std::string plain(b.size() - kHelloHeaderBytes - kTagBytes, '\0');
  unsigned long long mlen = 0;
  if (crypto_aead_chacha20poly1305_ietf_decrypt(reinterpret_cast<uint8_t*>(&plain[0]), &mlen, nullptr, &b[kHelloHeaderBytes],
                                                b.size() - kHelloHeaderBytes, b.data(), 42, &b[42], out.session.p2d.data()) != 0)
    return OpenError::Auth;
  plain.resize(mlen);
  out.plain = std::move(plain);
  return OpenError::None;
}

std::string qrText(const Pid& pid, const Key& paperPk, const std::string& haUrl) {
  std::string url = haUrl;
  while (!url.empty() && url.back() == '/') url.pop_back();
  return "paperos1:" + b64(pid) + ":" + b64(paperPk) + ":" + url;
}

}  // namespace pos::companion
