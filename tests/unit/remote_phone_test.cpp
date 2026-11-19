// Remote control from the paired phone (core/remote_phone.h, P7-21). The values were made with Python's hmac module from
// k_p2d of tests/fixtures/companion/vectors.json; the companion app's RemoteKeyTest checks the same ones.
#include <doctest.h>

#include <string>

#include "core/remote_phone.h"

using namespace pos;

namespace {

companion::Key fromHex(const std::string& hex) {
  companion::Key k{};
  for (size_t i = 0; i < k.size(); ++i) k[i] = static_cast<uint8_t>(std::stoi(hex.substr(2 * i, 2), nullptr, 16));
  return k;
}

const char kP2d[] = "1efe7567b67386fc9d999a840aa50612210dc2e7c88a6349a5b15dcd6d0b20c1";
const char kRemoteKey[] = "11d342f40c8190135181b05ae1957219d7456ca3343ee39672acb39f7f1e8175";
const char kNonce[] = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
const char kProof[] = "012199eacab31abc2c8e56351a8598c2d8f3393c532ad5558f9836bee7b1ce9e";

}  // namespace

TEST_CASE("remote phone: the key from the pairing and the proof of a nonce match the vectors") {
  const companion::Key key = remotephone::remoteKey(fromHex(kP2d));
  CHECK(key == fromHex(kRemoteKey));
  CHECK(remotephone::proof(key, kNonce) == kProof);
}

TEST_CASE("remote phone: the sign-in line opens for this nonce and key only") {
  const companion::Key key = fromHex(kRemoteKey);
  const std::string line = std::string(">auth-phone ") + kProof;
  CHECK(remotephone::isPhoneSignIn(line));
  CHECK_FALSE(remotephone::isPhoneSignIn(">auth 00"));
  CHECK(remotephone::authOk(kNonce, line, key));
  CHECK(remotephone::authOk(kNonce, line + "\r", key));
  std::string upper = line;
  for (size_t i = 12; i < upper.size(); ++i) upper[i] = static_cast<char>(std::toupper(upper[i]));
  CHECK(remotephone::authOk(kNonce, upper, key));
  std::string flipped = line;
  flipped.back() = flipped.back() == '0' ? '1' : '0';
  CHECK_FALSE(remotephone::authOk(kNonce, flipped, key));
  CHECK_FALSE(remotephone::authOk(kNonce, line.substr(0, line.size() - 2), key));
  std::string otherNonce = kNonce;
  otherNonce[0] = 'f';
  CHECK_FALSE(remotephone::authOk(otherNonce, line, key));
  companion::Key otherKey = key;
  otherKey[0] ^= 1;
  CHECK_FALSE(remotephone::authOk(kNonce, line, otherKey));
  CHECK_FALSE(remotephone::authOk("", line, key));
  CHECK_FALSE(remotephone::authOk(kNonce, std::string(">auth ") + kProof, key));  // the Mac's word
}

TEST_CASE("remote phone: what a phone session may send") {
  for (auto k : {RemoteCommand::Shot, RemoteCommand::Refresh, RemoteCommand::Tap, RemoteCommand::Long, RemoteCommand::Swipe})
    CHECK(remotephone::allowedForPhone(k));
  for (auto k : {RemoteCommand::Log, RemoteCommand::Reboot, RemoteCommand::Set, RemoteCommand::Status, RemoteCommand::Put,
                 RemoteCommand::Ls, RemoteCommand::Rm, RemoteCommand::PowerOff, RemoteCommand::Cat, RemoteCommand::State,
                 RemoteCommand::Ota, RemoteCommand::None})
    CHECK_FALSE(remotephone::allowedForPhone(k));
}

TEST_CASE("remote phone: no pairing, no phone sign-in") {
  CHECK_FALSE(remotephone::pairedAuthOk(kNonce, std::string(">auth-phone ") + kProof));
}
