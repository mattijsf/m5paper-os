// The Paper's side of the companion protocol (core/companion, docs/COMPANION.md): pairing, queue handling, replay, envelopes.
#include <doctest.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

#include "core/companion.h"
#include "core/fs.h"

using namespace pos;
using namespace pos::companion;

namespace {

struct Roots {
  std::string dir;
  Roots() {
    char tmpl[] = "/tmp/pos-companion-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    fs::mkdirs("/data");
  }
  ~Roots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

// A paired Paper and the phone's keys, from fixed bytes.
struct Pair {
  Pairing paper;
  KeyPair phone;
  Pair() {
    for (int i = 0; i < 32; ++i) paper.sk[i] = static_cast<uint8_t>(i), phone.sk[i] = static_cast<uint8_t>(0x40 + i);
    publicKeyOf(paper.sk, paper.pk);
    publicKeyOf(phone.sk, phone.pk);
    for (int i = 0; i < 8; ++i) paper.pid[i] = static_cast<uint8_t>(0xa1 + i);
    paper.phonePk = phone.pk;
    paper.phoneName = "Test Phone";
    paper.paired = true;
    REQUIRE(deriveSession(paper.sk, phone.pk, paper.pid, paper.pk, phone.pk, paper.session));
  }
  std::string fromPhone(uint64_t ctr, const std::string& plain) const { return seal(paper.session.p2d, kDirP2D, paper.pid, ctr, plain); }
};

std::string state(const std::vector<std::pair<uint64_t, std::string>>& q) {
  JsonDocument d;
  d["entity_id"] = kToPaperEntity;
  d["state"] = "1";
  JsonArray a = d["attributes"]["queue"].to<JsonArray>();
  for (auto& e : q) {
    JsonObject o = a.add<JsonObject>();
    o["id"] = e.first;
    o["ts"] = 1768480000;
    o["e"] = e.second;
  }
  std::string s;
  serializeJson(d, s);
  return s;
}

const std::string kPostA = R"({"t":"post","k":"wa-1","app":"whatsapp","chat":"Family","reply":true,"ts":1768480000,"msgs":[{"from":"Anna","text":"Six?","ts":1768479990},{"from":"Bram","text":"Yes","ts":1768480000}]})";
const std::string kPostMail = R"({"t":"post","k":"mail-1","app":"mail","chat":"","reply":false,"ts":1768470000,"msgs":[{"from":"Shop","subj":"Your order","text":"Ready","ts":1768470000}]})";
const int64_t kNow = 1768480100;

}  // namespace

TEST_CASE("companion: posts, removes and syncs are applied in counter order; read entries are acked") {
  Pair pr;
  Notifs n;
  // Out of order in the queue: 2 (mail) before 1 (WhatsApp); 3 removes the mail.
  ApplyResult r = applyQueue(pr.paper, n, state({{2, pr.fromPhone(2, kPostMail)}, {1, pr.fromPhone(1, kPostA)}}), kNow);
  CHECK(r.parsed);
  CHECK(r.accepted == 2);
  CHECK(r.ack);
  CHECK(pr.paper.rx == 2);
  REQUIRE(n.items.size() == 2);
  const Notif* wa = findNotif(n, "wa-1");
  REQUIRE(wa);
  CHECK(wa->chat == "Family");
  CHECK(wa->reply);
  REQUIRE(wa->msgs.size() == 2);
  CHECK(wa->msgs[1].from == "Bram");
  CHECK(findNotif(n, "mail-1")->msgs[0].subject == "Your order");

  // The same queue again (no ack happened): nothing new, but ack again.
  r = applyQueue(pr.paper, n, state({{1, pr.fromPhone(1, kPostA)}, {2, pr.fromPhone(2, kPostMail)}}), kNow);
  CHECK(r.accepted == 0);
  CHECK(r.ack);

  r = applyQueue(pr.paper, n, state({{3, pr.fromPhone(3, R"({"t":"remove","k":"mail-1","why":"read"})")}}), kNow);
  CHECK(r.accepted == 1);
  CHECK(findNotif(n, "mail-1") == nullptr);
  r = applyQueue(pr.paper, n, state({{4, pr.fromPhone(4, kPostMail)}, {5, pr.fromPhone(5, R"({"t":"sync","keys":["mail-1"]})")}}), kNow);
  CHECK(r.accepted == 2);
  CHECK(findNotif(n, "wa-1") == nullptr);
  CHECK(findNotif(n, "mail-1") != nullptr);
}

TEST_CASE("companion: a post replaces the same key; empty or unknown posts are ignored") {
  Pair pr;
  Notifs n;
  applyQueue(pr.paper, n, state({{1, pr.fromPhone(1, kPostA)}}), kNow);
  applyQueue(pr.paper, n,
             state({{2, pr.fromPhone(2, R"({"t":"post","k":"wa-1","app":"whatsapp","chat":"Family","reply":false,"msgs":[{"from":"Anna","text":"New","ts":1768480050}]})")},
                    {3, pr.fromPhone(3, R"({"t":"post","k":"x","app":"signal","msgs":[{"from":"A","text":"t"}]})")},
                    {4, pr.fromPhone(4, R"({"t":"later-type","k":"y"})")}}),
             kNow);
  REQUIRE(n.items.size() == 1);
  CHECK(n.items[0].msgs.size() == 1);
  CHECK(n.items[0].msgs[0].text == "New");
  CHECK_FALSE(n.items[0].reply);
  CHECK(n.items[0].ts == 1768480050);
  CHECK(pr.paper.rx == 4);
}

TEST_CASE("companion: replays, forgeries and mismatched ids are dropped") {
  Pair pr;
  Notifs n;
  applyQueue(pr.paper, n, state({{5, pr.fromPhone(5, kPostA)}}), kNow);
  CHECK(pr.paper.rx == 5);
  // Replay of an older counter under a new id, an id that does not match the counter, a Paper->phone envelope, garbage.
  const std::string old = pr.fromPhone(3, kPostMail);
  ApplyResult r = applyQueue(pr.paper, n,
                             state({{6, old}, {7, pr.fromPhone(8, kPostMail)}, {9, seal(pr.paper.session.d2p, kDirD2P, pr.paper.pid, 9, kPostMail)},
                                    {10, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}}),
                             kNow);
  CHECK(r.accepted == 0);
  CHECK(r.rejected == 4);
  CHECK(findNotif(n, "mail-1") == nullptr);
  CHECK(pr.paper.rx == 5);
  // Not paired: nothing is read.
  Pairing none;
  Notifs n2;
  r = applyQueue(none, n2, state({{1, pr.fromPhone(1, kPostA)}}), kNow);
  CHECK(r.accepted == 0);
  CHECK(n2.items.empty());
  CHECK_FALSE(applyQueue(pr.paper, n, "not json", kNow).parsed);
}

TEST_CASE("companion: results become toasts for known replies; unpair stops reading") {
  Pair pr;
  Notifs n;
  n.sent.push_back(SentReply{3, "Family"});
  n.sent.push_back(SentReply{4, "Anna"});
  ApplyResult r = applyQueue(pr.paper, n,
                             state({{1, pr.fromPhone(1, R"({"t":"result","ref":3,"ok":true,"why":""})")},
                                    {2, pr.fromPhone(2, R"({"t":"result","ref":4,"ok":false,"why":"gone"})")},
                                    {3, pr.fromPhone(3, R"({"t":"unpair"})")}, {4, pr.fromPhone(4, kPostA)}}),
                             kNow);
  REQUIRE(r.notes.size() == 2);
  CHECK(r.notes[0] == "Reply to Family sent");
  CHECK(r.notes[1] == "Reply to Anna not sent: the notification is gone from the phone");
  CHECK(n.sent.empty());
  CHECK(r.unpaired);
  CHECK(n.items.empty());  // nothing after the unpair is applied
}

TEST_CASE("companion: old notifications expire after 7 days") {
  Notifs n;
  n.items.push_back(Notif{"a", "whatsapp", "", false, kNow - 8 * 86400, {}});
  n.items.push_back(Notif{"b", "whatsapp", "", false, kNow - 3600, {}});
  CHECK(expire(n, kNow));
  REQUIRE(n.items.size() == 1);
  CHECK(n.items[0].key == "b");
}

TEST_CASE("companion: envelopes to the phone take the next counter and open with the phone's keys") {
  Pair pr;
  Session phone;
  REQUIRE(deriveSession(pr.phone.sk, pr.paper.pk, pr.paper.pid, pr.paper.pk, pr.phone.pk, phone));
  const std::string body = replyEnvelope(pr.paper, "wa-1", "  See you \"there\"  ", kNow);
  CHECK(pr.paper.tx == 1);
  JsonDocument d;
  REQUIRE(!deserializeJson(d, body));
  CHECK(d["queue"] == "to_phone");
  CHECK(d["id"] == 1);
  Opened o;
  REQUIRE(open(phone.d2p, kDirD2P, pr.paper.pid, d["env"].as<std::string>(), o) == OpenError::None);
  JsonDocument m;
  REQUIRE(!deserializeJson(m, o.plain));
  CHECK(m["t"] == "reply");
  CHECK(m["k"] == "wa-1");
  CHECK(m["ts"] == kNow);
  CHECK(!pairedEnvelope(pr.paper).empty());
  CHECK(!dismissEnvelope(pr.paper, {"a", "b"}).empty());
  CHECK(pr.paper.tx == 3);
  CHECK(replyEnvelope(pr.paper, "k", std::string(2000, 'x'), kNow).size() < 1500);  // text cut to 900 characters
  CHECK(ackBody("to_paper", 17) == R"({"queue":"to_paper","upto":17})");
  Pairing unpaired;
  CHECK(replyEnvelope(unpaired, "k", "hi", kNow).empty());
}

TEST_CASE("companion: pairing from a hello, saved and loaded again") {
  Roots roots;
  setTestRandom(42);
  PendingPairing pp = startPairing();
  setTestRandom(0);
  const KeyPair phone = makeKeyPair();
  Session ph;
  REQUIRE(deriveSession(phone.sk, pp.keys.pk, pp.pid, pp.keys.pk, phone.pk, ph));
  JsonDocument d;
  d["attributes"]["queue"][0]["id"] = 0;
  d["attributes"]["queue"][0]["e"] = sealHello(ph.p2d, pp.pid, phone.pk, R"({"t":"hello","name":"Pixel 9 Pro","app":"0.1"})");
  std::string s;
  serializeJson(d, s);
  HelloResult h = readHello(pp, s);
  REQUIRE(h.found);
  CHECK(h.phoneName == "Pixel 9 Pro");
  CHECK(h.code == codeText(ph.code));
  // A hello for another pid is not taken.
  PendingPairing other = startPairing();
  CHECK_FALSE(readHello(other, s).found);

  h.pairing.tx = 3;
  h.pairing.rx = 9;
  REQUIRE(savePairing(h.pairing));
  Pairing back;
  REQUIRE(loadPairing(back));
  CHECK(back.paired);
  CHECK(back.tx == 3);
  CHECK(back.rx == 9);
  CHECK(back.phoneName == "Pixel 9 Pro");
  CHECK(back.session.p2d == ph.p2d);
  forgetPairing();
  CHECK_FALSE(loadPairing(back));
}

TEST_CASE("companion: the notification store survives a save and load") {
  Roots roots;
  Pair pr;
  Notifs n;
  applyQueue(pr.paper, n, state({{1, pr.fromPhone(1, kPostA)}, {2, pr.fromPhone(2, kPostMail)}}), kNow);
  n.sent.push_back(SentReply{7, "Family"});
  REQUIRE(saveNotifs(n));
  Notifs back;
  REQUIRE(loadNotifs(back));
  REQUIRE(back.items.size() == 2);
  CHECK(back.items[0].msgs[1].text == "Yes");
  CHECK(back.items[1].msgs[0].subject == "Your order");
  REQUIRE(back.sent.size() == 1);
  CHECK(back.sent[0].ref == 7);
}
