// Companion protocol v1 crypto (docs/COMPANION.md sections 1 to 5): X25519, HKDF-SHA256, ChaCha20-Poly1305 IETF, the envelope
// layout and base64url. One code path for the simulator and the firmware: libsodium 1.0.22 (Homebrew on the Mac, the
// espressif__libsodium component on the device). Pure functions; nothing here logs or stores a key. Unit-tested against
// tests/fixtures/companion/vectors.json (made with another implementation).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pos::companion {

constexpr size_t kKeyBytes = 32, kPidBytes = 8, kNonceBytes = 12, kTagBytes = 16;
constexpr size_t kHeaderBytes = 30;       // version, dir, pid, counter, nonce
constexpr size_t kHelloHeaderBytes = 54;  // version, dir, pid, phone_pk, nonce
constexpr size_t kMaxPlain = 1000;        // plaintext bytes
constexpr size_t kMaxEnvChars = 1395;     // base64url characters of a normal envelope
constexpr size_t kMaxHelloChars = 1411;
constexpr char kDirP2D = 'P', kDirD2P = 'D', kDirHello = 'H';

using Key = std::array<uint8_t, kKeyBytes>;
using Pid = std::array<uint8_t, kPidBytes>;

// sodium_init once; false if libsodium cannot start (then nothing here works).
bool cryptoInit();

// Random bytes from the CSPRNG. setTestRandom(seed != 0) makes them deterministic for UI tests and fixtures: call i (from 0
// after the call) gives 32-byte blocks SHA-256("paperos-test-random" || seed (4 bytes BE) || i (4 bytes BE)), one index per block, cut to
// the length asked (tests/fixtures/make-inbox-fixtures.py does the same). Never set on the device.
void randomBytes(uint8_t* out, size_t n);
void setTestRandom(uint32_t seed);

struct KeyPair {
  Key sk{}, pk{};
};
KeyPair makeKeyPair();
bool publicKeyOf(const Key& sk, Key& pk);

// Section 3: both directional keys and the comparison code from one side's secret key and the other side's public key.
struct Session {
  Key p2d{}, d2p{};
  uint32_t code = 0;  // 0 .. 999999
};
bool deriveSession(const Key& ownSk, const Key& peerPk, const Pid& pid, const Key& paperPk, const Key& phonePk, Session& out);
// "123 456" (six digits, leading zeros kept).
std::string codeText(uint32_t code);

// base64url without padding.
std::string b64(const uint8_t* data, size_t n);
template <size_t N>
std::string b64(const std::array<uint8_t, N>& a) { return b64(a.data(), N); }
// Strict decode: only [A-Za-z0-9_-], no padding; false on anything else.
bool unb64(const std::string& text, std::vector<uint8_t>& out);
template <size_t N>
bool unb64Fixed(const std::string& text, std::array<uint8_t, N>& out) {
  std::vector<uint8_t> v;
  if (!unb64(text, v) || v.size() != N) return false;
  for (size_t i = 0; i < N; ++i) out[i] = v[i];
  return true;
}

// Section 4.1. `nonce` null = random (tests pass the vectors' nonces). "" if the plaintext is too long.
std::string seal(const Key& key, char dir, const Pid& pid, uint64_t counter, const std::string& plain, const uint8_t* nonce = nullptr);

enum class OpenError : uint8_t { None, Size, Format, Version, Dir, Pid, Auth };
const char* openErrorText(OpenError e);
struct Opened {
  uint64_t counter = 0;
  std::string plain;
};
// Checks length, base64url, version, dir and pid, then authenticates and decrypts.
OpenError open(const Key& key, char dir, const Pid& pid, const std::string& env, Opened& out);

// Section 4.2: the phone's hello. Reads phone_pk from it, derives the session (the Paper's side) and decrypts.
struct Hello {
  Key phonePk{};
  Session session;
  std::string plain;
};
OpenError openHello(const Key& paperSk, const Key& paperPk, const Pid& pid, const std::string& env, Hello& out);
// The phone's side of a hello (for tests and fixtures).
std::string sealHello(const Key& p2d, const Pid& pid, const Key& phonePk, const std::string& plain, const uint8_t* nonce = nullptr);

// Section 2: the QR code text, "paperos1:<pid>:<paper_pk>:<url>" (the URL without a trailing slash).
std::string qrText(const Pid& pid, const Key& paperPk, const std::string& haUrl);

}  // namespace pos::companion
