#include "core/remote_phone.h"

#include <sodium.h>

#include "core/companion.h"
#include "core/ha.h"
#include "core/log.h"
#include "core/os.h"

namespace pos::remotephone {

namespace {

constexpr char kWord[] = ">auth-phone ";
constexpr char kInfo[] = "PaperOS remote";
constexpr uint32_t kRetryMs = 5 * 60 * 1000;  // a failed push (or no phone paired yet): try again after this

}  // namespace

companion::Key remoteKey(const companion::Key& p2d) {
  companion::Key out{};
  if (sodium_init() < 0) return out;
  crypto_kdf_hkdf_sha256_expand(out.data(), out.size(), kInfo, sizeof kInfo - 1, p2d.data());
  return out;
}

std::string proof(const companion::Key& key, const std::string& nonceHex) {
  const std::string msg = "paperos-remote:" + nonceHex;
  uint8_t mac[crypto_auth_hmacsha256_BYTES];
  if (sodium_init() < 0) return "";
  crypto_auth_hmacsha256_state st;
  crypto_auth_hmacsha256_init(&st, key.data(), key.size());
  crypto_auth_hmacsha256_update(&st, reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
  crypto_auth_hmacsha256_final(&st, mac);
  static const char kHex[] = "0123456789abcdef";
  std::string hex;
  for (const uint8_t b : mac) {
    hex += kHex[b >> 4];
    hex += kHex[b & 15];
  }
  return hex;
}

bool isPhoneSignIn(const std::string& line) { return line.compare(0, sizeof kWord - 1, kWord) == 0; }

bool authOk(const std::string& nonceHex, const std::string& line, const companion::Key& key) {
  if (nonceHex.size() != 2 * update::kNonceBytes || !isPhoneSignIn(line)) return false;
  std::string got = line.substr(sizeof kWord - 1);
  while (!got.empty() && (got.back() == '\r' || got.back() == ' ')) got.pop_back();
  for (char& c : got)
    if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
  const std::string want = proof(key, nonceHex);
  if (want.empty() || got.size() != want.size()) return false;
  return sodium_memcmp(got.data(), want.data(), want.size()) == 0;
}

bool pairedAuthOk(const std::string& nonceHex, const std::string& line) {
  companion::Pairing p;
  if (!companion::loadPairing(p) || !p.paired) return false;
  const companion::Key key = remoteKey(p.session.p2d);
  return authOk(nonceHex, line, key);
}

bool allowedForPhone(RemoteCommand::Kind kind) {
  switch (kind) {
    case RemoteCommand::Shot:
    case RemoteCommand::Refresh:
    case RemoteCommand::Tap:
    case RemoteCommand::Long:
    case RemoteCommand::Swipe:
      return true;
    default:
      return false;
  }
}

void announce(Os& os, const std::string& address, uint16_t port, uint32_t nowMs) {
  static std::string told, tried;  // the address the phone was told about (this boot); the one last tried
  static uint32_t triedMs = 0;
  if (address.empty() || address == told) return;
  if (address == tried && nowMs - triedMs < kRetryMs) return;  // also: a push on its way, or no phone paired
  tried = address;
  triedMs = nowMs;
  if (!companion::pairedPhone()) return;
  companion::Pairing p;
  if (!companion::loadPairing(p)) return;
  // The counter is stored before the push (COMPANION.md 5).
  const std::string body = companion::remoteEnvelope(p, address, port);
  if (body.empty() || !companion::savePairing(p)) return;
  HaClient ha = HaClient::fromNet(os.net(), os.secrets());
  os.worker().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); },
                    [address](HaResult r) {
                      POS_LOG("remote", "address to the phone: %s", r.ok() ? "pushed" : haErrorText(r.error));
                      if (r.ok()) told = address;
                    });
}

}  // namespace pos::remotephone
