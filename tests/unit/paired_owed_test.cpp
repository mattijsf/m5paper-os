// The `paired` envelope the phone may not have yet (core/paired_owed, docs/COMPANION.md 2 step 4).
#include <doctest.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

#include "core/companion.h"
#include "core/fs.h"
#include "core/paired_owed.h"

using namespace pos;
using namespace pos::companion;

namespace {

struct Roots {
  std::string dir;
  Roots() {
    char tmpl[] = "/tmp/pos-paired-owed-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    fs::mkdirs("/data");
  }
  ~Roots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

const int64_t kNow = 1768480100;

// A stored pairing with fixed keys; returns the phone's session to open what the Paper sends.
Session storePairing(uint64_t tx, uint64_t rx) {
  Pairing p;
  KeyPair phone;
  for (int i = 0; i < 32; ++i) p.sk[i] = static_cast<uint8_t>(i), phone.sk[i] = static_cast<uint8_t>(0x40 + i);
  publicKeyOf(p.sk, p.pk);
  publicKeyOf(phone.sk, phone.pk);
  for (int i = 0; i < 8; ++i) p.pid[i] = static_cast<uint8_t>(0xa1 + i);
  p.phonePk = phone.pk;
  p.phoneName = "Test Phone";
  p.paired = true;
  p.tx = tx;
  p.rx = rx;
  REQUIRE(savePairing(p));
  Session s;
  REQUIRE(deriveSession(phone.sk, p.pk, p.pid, p.pk, phone.pk, s));
  return s;
}

}  // namespace

TEST_CASE("paired owed: resent as counter 1 until the push lands, without touching the stored counters") {
  Roots roots;
  const Session phone = storePairing(3, 0);  // a clip and a dismiss went out after `paired`
  CHECK(pairedOwed(kNow) == "");             // nothing marked
  markPairedOwed(kNow);
  const std::string body = pairedOwed(kNow + 60);
  REQUIRE(!body.empty());
  JsonDocument d;
  REQUIRE(!deserializeJson(d, body));
  CHECK(d["queue"] == "to_phone");
  CHECK(d["id"] == 1);
  Pairing back;
  REQUIRE(loadPairing(back));
  Opened o;
  REQUIRE(open(phone.d2p, kDirD2P, back.pid, d["env"].as<std::string>(), o) == OpenError::None);
  CHECK(o.counter == 1);
  CHECK(o.plain == R"({"t":"paired"})");
  CHECK(back.tx == 3);  // nothing saved
  CHECK(!pairedOwed(kNow + 120).empty());  // still owed: only a landed push clears it
  markPairedSent();
  CHECK(pairedOwed(kNow + 180) == "");
}

TEST_CASE("paired owed: dropped once the phone has written, after a day, or when unpaired") {
  Roots roots;
  storePairing(1, 2);  // the phone sent its sync: it is active, so it has `paired`
  markPairedOwed(kNow);
  CHECK(pairedOwed(kNow + 60) == "");
  storePairing(1, 0);
  CHECK(pairedOwed(kNow + 60) == "");  // the mark went with the first check

  markPairedOwed(kNow);
  CHECK(!pairedOwed(kNow + kPairedOwedMax).empty());
  CHECK(pairedOwed(kNow + kPairedOwedMax + 1) == "");

  markPairedOwed(kNow);
  forgetPairing();
  CHECK(pairedOwed(kNow + 60) == "");
  fs::writeFile("/data/companion_paired_owed", "garbage");
  storePairing(1, 0);
  CHECK(pairedOwed(kNow + 60) == "");
}
