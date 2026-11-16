// docs/COMPANION.md v1 against tests/fixtures/companion/vectors.json (made with pyca/cryptography, not libsodium) and RFC vectors.
#include <doctest.h>

#include <ArduinoJson.h>

#include <cstdio>
#include <string>

#include "core/companion_crypto.h"

using namespace pos::companion;

namespace {

std::string readAll(const char* path) {
  std::string s;
  if (FILE* f = std::fopen(path, "rb")) {
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
  }
  return s;
}

std::vector<uint8_t> hex(const std::string& h) {
  std::vector<uint8_t> v;
  for (size_t i = 0; i + 1 < h.size(); i += 2) v.push_back(static_cast<uint8_t>(std::stoul(h.substr(i, 2), nullptr, 16)));
  return v;
}
template <size_t N>
std::array<uint8_t, N> hexA(const std::string& h) {
  std::array<uint8_t, N> a{};
  auto v = hex(h);
  REQUIRE(v.size() == N);
  for (size_t i = 0; i < N; ++i) a[i] = v[i];
  return a;
}
template <size_t N>
std::string toHex(const std::array<uint8_t, N>& a) {
  std::string s;
  char b[3];
  for (uint8_t x : a) {
    std::snprintf(b, sizeof b, "%02x", x);
    s += b;
  }
  return s;
}

struct Vectors {
  JsonDocument doc;
  Key paperSk, phoneSk, paperPk, phonePk;
  Pid pid;
  Vectors() {
    const std::string text = readAll("tests/fixtures/companion/vectors.json");
    REQUIRE(!text.empty());
    REQUIRE(!deserializeJson(doc, text));
    paperSk = hexA<32>(doc["paper_sk"].as<std::string>());
    phoneSk = hexA<32>(doc["phone_sk"].as<std::string>());
    paperPk = hexA<32>(doc["paper_pk"].as<std::string>());
    phonePk = hexA<32>(doc["phone_pk"].as<std::string>());
    pid = hexA<8>(doc["pid"].as<std::string>());
  }
};

}  // namespace

TEST_CASE("companion crypto: RFC 7748 X25519 vector (Alice's key pair and the shared secret)") {
  REQUIRE(cryptoInit());
  const Key aliceSk = hexA<32>("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
  const Key bobPk = hexA<32>("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
  Key alicePk;
  REQUIRE(publicKeyOf(aliceSk, alicePk));
  CHECK(toHex(alicePk) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
  // The shared secret itself is not exposed; a session from it must match the one Bob would derive.
  const Key bobSk = hexA<32>("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
  Key bobPk2;
  REQUIRE(publicKeyOf(bobSk, bobPk2));
  CHECK(toHex(bobPk2) == toHex(bobPk));
  Pid pid{};
  Session a, b;
  REQUIRE(deriveSession(aliceSk, bobPk, pid, alicePk, bobPk, a));
  REQUIRE(deriveSession(bobSk, alicePk, pid, alicePk, bobPk, b));
  CHECK(toHex(a.p2d) == toHex(b.p2d));
  CHECK(toHex(a.d2p) == toHex(b.d2p));
  CHECK(a.code == b.code);
}

TEST_CASE("companion crypto: an all-zero shared secret (low-order point) is refused") {
  Key sk{};
  sk[0] = 1;
  Key zeroPk{};
  Session s;
  CHECK_FALSE(deriveSession(sk, zeroPk, Pid{}, Key{}, Key{}, s));
}

TEST_CASE("companion crypto: key schedule and code match the shared vectors from both sides") {
  Vectors v;
  Key pk;
  REQUIRE(publicKeyOf(v.paperSk, pk));
  CHECK(toHex(pk) == v.doc["paper_pk"].as<std::string>());
  REQUIRE(publicKeyOf(v.phoneSk, pk));
  CHECK(toHex(pk) == v.doc["phone_pk"].as<std::string>());
  Session paper, phone;
  REQUIRE(deriveSession(v.paperSk, v.phonePk, v.pid, v.paperPk, v.phonePk, paper));
  REQUIRE(deriveSession(v.phoneSk, v.paperPk, v.pid, v.paperPk, v.phonePk, phone));
  CHECK(toHex(paper.p2d) == v.doc["k_p2d"].as<std::string>());
  CHECK(toHex(paper.d2p) == v.doc["k_d2p"].as<std::string>());
  CHECK(toHex(phone.p2d) == toHex(paper.p2d));
  CHECK(codeText(paper.code) == v.doc["code"].as<std::string>().substr(0, 3) + " " + v.doc["code"].as<std::string>().substr(3));
  CHECK(codeText(7) == "000 007");
  CHECK(qrText(v.pid, v.paperPk, "https://ha.example.test/") == v.doc["qr_text"].as<std::string>());
  CHECK(b64(v.pid) == v.doc["pid_b64"].as<std::string>());
}

TEST_CASE("companion crypto: seal reproduces the vectors' envelopes; open and openHello read them") {
  Vectors v;
  Session s;
  REQUIRE(deriveSession(v.paperSk, v.phonePk, v.pid, v.paperPk, v.phonePk, s));
  const auto post = v.doc["post"], reply = v.doc["reply"], hello = v.doc["hello"];
  CHECK(seal(s.p2d, kDirP2D, v.pid, 1, post["plaintext"].as<std::string>(), hex(post["nonce"].as<std::string>()).data()) ==
        post["envelope"].as<std::string>());
  CHECK(seal(s.d2p, kDirD2P, v.pid, 1, reply["plaintext"].as<std::string>(), hex(reply["nonce"].as<std::string>()).data()) ==
        reply["envelope"].as<std::string>());
  CHECK(sealHello(s.p2d, v.pid, v.phonePk, hello["plaintext"].as<std::string>(), hex(hello["nonce"].as<std::string>()).data()) ==
        hello["envelope"].as<std::string>());

  Opened o;
  REQUIRE(open(s.p2d, kDirP2D, v.pid, post["envelope"].as<std::string>(), o) == OpenError::None);
  CHECK(o.counter == 1);
  CHECK(o.plain == post["plaintext"].as<std::string>());
  Hello h;
  REQUIRE(openHello(v.paperSk, v.paperPk, v.pid, hello["envelope"].as<std::string>(), h) == OpenError::None);
  CHECK(toHex(h.phonePk) == v.doc["phone_pk"].as<std::string>());
  CHECK(h.plain == hello["plaintext"].as<std::string>());
  CHECK(h.session.code == s.code);
}

TEST_CASE("companion crypto: tampered, foreign and oversize envelopes are rejected") {
  Vectors v;
  Session s;
  REQUIRE(deriveSession(v.paperSk, v.phonePk, v.pid, v.paperPk, v.phonePk, s));
  const auto r = v.doc["reject_to_paper"];
  Opened o;
  CHECK(open(s.p2d, kDirP2D, v.pid, r["flipped_tag"].as<std::string>(), o) == OpenError::Auth);
  CHECK(open(s.p2d, kDirP2D, v.pid, r["flipped_counter"].as<std::string>(), o) == OpenError::Auth);
  CHECK(open(s.p2d, kDirP2D, v.pid, r["wrong_pid"].as<std::string>(), o) == OpenError::Pid);
  CHECK(open(s.p2d, kDirP2D, v.pid, r["wrong_dir_reply_as_post"].as<std::string>(), o) == OpenError::Dir);
  CHECK(open(s.p2d, kDirP2D, v.pid, r["oversize"].as<std::string>(), o) == OpenError::Size);
  CHECK(open(s.p2d, kDirP2D, v.pid, "AQ==", o) == OpenError::Format);           // padding is not allowed
  CHECK(open(s.p2d, kDirP2D, v.pid, "AQ", o) == OpenError::Size);
  CHECK(open(s.d2p, kDirP2D, v.pid, v.doc["post"]["envelope"].as<std::string>(), o) == OpenError::Auth);  // wrong key
  CHECK(seal(s.p2d, kDirP2D, v.pid, 1, std::string(1001, 'x')).empty());
  CHECK(std::string(openErrorText(OpenError::Auth)) == "auth");
}

TEST_CASE("companion crypto: random nonces differ, test random is repeatable") {
  Vectors v;
  Session s;
  REQUIRE(deriveSession(v.paperSk, v.phonePk, v.pid, v.paperPk, v.phonePk, s));
  CHECK(seal(s.d2p, kDirD2P, v.pid, 2, "{}") != seal(s.d2p, kDirD2P, v.pid, 2, "{}"));
  setTestRandom(7);
  const KeyPair a = makeKeyPair();
  setTestRandom(7);
  const KeyPair b = makeKeyPair();
  setTestRandom(0);
  CHECK(toHex(a.sk) == toHex(b.sk));
  CHECK(toHex(a.pk) == toHex(b.pk));
  std::vector<uint8_t> out;
  CHECK(unb64("_-AZaz09", out));
  CHECK_FALSE(unb64("a+b", out));
  CHECK_FALSE(unb64("a=", out));
}
