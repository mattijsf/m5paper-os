// The Paper's side of the companion protocol (core/companion, docs/COMPANION.md): pairing, queue handling, replay, envelopes.
#include <doctest.h>

#include <ArduinoJson.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

#include "core/companion.h"
#include "apps/inbox/scheduled_refresh.h"
#include "core/fs.h"
#include "core/store.h"

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
  std::string name;
  CHECK(pairedPhone(&name));  // the question the screens ask, without deriving keys
  CHECK(name == "Pixel 9 Pro");
  forgetPairing();
  CHECK_FALSE(loadPairing(back));
  CHECK_FALSE(pairedPhone());
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

TEST_CASE("inbox: the launcher badges of both apps follow the store: what is newer than each app's seen marker") {
  Roots roots;
  Notifs n;
  n.items.push_back(Notif{"w1", "whatsapp", "Family", true, 200, {NotifMsg{"Anna", "", "Hi", 150}, NotifMsg{"Anna", "", "There?", 200}}});
  n.items.push_back(Notif{"m1", "mail", "", false, 300, {NotifMsg{"Shop", "Order", "Ready", 300}}});
  // Nobody has looked yet: everything counts.
  CHECK(pos::inbox::writeBadges(n) == 3);
  CHECK(appBadge("whatsapp") == 2);
  CHECK(appBadge("mail") == 1);
  // WhatsApp was opened (its seen marker is its newest message); the mail is still unread. Whoever reads the relay
  // next (here: again the same store) leaves WhatsApp at 0 and Mail at 1: the badge on the launcher.
  Store whatsapp("/data/apps/whatsapp.json");
  whatsapp.set("seen", 200);
  REQUIRE(whatsapp.save());
  CHECK(pos::inbox::writeBadges(n) == 1);
  CHECK(appBadge("whatsapp") == 0);
  CHECK(appBadge("mail") == 1);
  n.items[0].msgs.push_back(NotifMsg{"Anna", "", "Hello?", 250});  // a new message while Mail was open
  CHECK(pos::inbox::writeBadges(n) == 2);
  CHECK(appBadge("whatsapp") == 1);
  CHECK(pos::inbox::writeBadges(n, true) == 0);  // no feed can exist (unpaired): nothing to show
  CHECK(appBadge("mail") == 0);
}

TEST_CASE("companion: a notification is cleaned when it arrives; a store from before that is cleaned when loaded") {
  Roots roots;
  Pair pr;
  Notifs n;
  const std::string post = R"({"t":"post","k":"m9","app":"mail","ts":1768470000,"msgs":[{"from":"Shop\n","subj":"Your\norder",)"
                           R"("text":"<p>Hello &amp; welcome</p><p>See https://www.example.com/a?b=1</p>","ts":1768470000}]})";
  applyQueue(pr.paper, n, state({{1, pr.fromPhone(1, post)}}), kNow);
  REQUIRE(n.items.size() == 1);
  CHECK(n.items[0].msgs[0].text == "Hello & welcome\n\nSee example.com");  // already what the panel shows
  CHECK(n.items[0].msgs[0].from == "Shop");
  CHECK(n.items[0].msgs[0].subject == "Your order");
  REQUIRE(saveNotifs(n));
  std::string file;
  REQUIRE(fs::readFile("/data/cache/inbox/notifs.json", file));
  CHECK(file.find("\"clean\":true") != std::string::npos);

  // A file of an older firmware: raw text, no mark.
  REQUIRE(fs::writeFile("/data/cache/inbox/notifs.json",
                        R"({"items":[{"k":"old","app":"mail","chat":"","reply":false,"ts":5,)"
                        R"("msgs":[{"from":"A","subj":"S","text":"<div>Old &lt;raw&gt; text</div>","ts":5}]}],"sent":[]})"));
  Notifs old;
  REQUIRE(loadNotifs(old));
  REQUIRE(old.items.size() == 1);
  CHECK(old.items[0].msgs[0].text == "Old <raw> text");
}

TEST_CASE("companion: clip envelopes both ways (D23): the phone's newest clip wins, old or unwanted ones are dropped") {
  Pair pr;
  Notifs n;
  const std::string clipOld = R"({"t":"clip","text":"first","ts":1768480000})";
  const std::string clipNew = R"({"t":"clip","text":"Somdoku 12:34\nno mistakes","ts":1768480050})";
  ApplyResult r = applyQueue(pr.paper, n, state({{1, pr.fromPhone(1, clipOld)}, {2, pr.fromPhone(2, clipNew)}}), kNow);
  CHECK(r.accepted == 2);
  CHECK(r.hasClip);
  CHECK(r.clip == "Somdoku 12:34\nno mistakes");
  CHECK(n.items.empty());  // a clip is not a notification

  ApplyResult off = applyQueue(pr.paper, n, state({{3, pr.fromPhone(3, clipNew)}}), kNow, false);
  CHECK(off.accepted == 1);  // read (and acked), not applied
  CHECK_FALSE(off.hasClip);

  const std::string stale = R"({"t":"clip","text":"yesterday","ts":)" + std::to_string(kNow - kClipMaxAge - 60) + "}";
  const std::string empty = R"({"t":"clip","text":"","ts":1768480050})";
  ApplyResult old = applyQueue(pr.paper, n, state({{4, pr.fromPhone(4, stale)}, {5, pr.fromPhone(5, empty)}}), kNow);
  CHECK(old.accepted == 2);
  CHECK_FALSE(old.hasClip);

  Session phone;
  REQUIRE(deriveSession(pr.phone.sk, pr.paper.pk, pr.paper.pid, pr.paper.pk, pr.phone.pk, phone));
  auto openClip = [&](const std::string& body) {
    JsonDocument d;
    REQUIRE(!deserializeJson(d, body));
    Opened o;
    REQUIRE(open(phone.d2p, kDirD2P, pr.paper.pid, d["env"].as<std::string>(), o) == OpenError::None);
    CHECK(o.plain.size() <= kMaxPlain);
    JsonDocument m;
    REQUIRE(!deserializeJson(m, o.plain));
    CHECK(m["t"] == "clip");
    CHECK(m["ts"] == kNow);
    return m["text"].as<std::string>();
  };
  CHECK(openClip(clipEnvelope(pr.paper, "Copied on the Paper", kNow)) == "Copied on the Paper");
  CHECK(pr.paper.tx == 1);
  CHECK(openClip(clipEnvelope(pr.paper, std::string(2000, 'x'), kNow)).size() == kMaxClipChars);
  const std::string quotes = openClip(clipEnvelope(pr.paper, std::string(900, '"'), kNow));  // each escapes to 2 bytes
  CHECK(quotes.size() < 500);
  CHECK(!quotes.empty());
  CHECK(clipEnvelope(pr.paper, "", kNow).empty());
  // A clip that is a link is also handed over for reading later (P7-18): with the clipboard not shared, and however old.
  const std::string link = R"({"t":"clip","text":"A page https://example.com/a","ts":)" + std::to_string(kNow - kClipMaxAge - 60) + "}";
  ApplyResult shared = applyQueue(pr.paper, n, state({{20, pr.fromPhone(20, link)}, {21, pr.fromPhone(21, clipNew)}}), kNow, false);
  CHECK_FALSE(shared.hasClip);
  REQUIRE(shared.links.size() == 1);
  CHECK(shared.links[0] == "A page https://example.com/a");
}

TEST_CASE("inbox: a message is new for the sound once: unread, and later than what an earlier read brought") {
  Roots roots;
  Pair pr;
  Notifs n;
  // The first read after pairing: two chat messages and a mail nobody saw.
  applyQueue(pr.paper, n, state({{1, pr.fromPhone(1, kPostA)}, {2, pr.fromPhone(2, kPostMail)}}), kNow);
  CHECK(pos::inbox::freshMessages(n) == 3);
  n.heard = newestMsgTs(n);  // what relay_fetch does after every read
  CHECK(n.heard == 1768480000);
  CHECK(pos::inbox::freshMessages(n) == 0);  // the next read brings nothing
  // The marker survives the store; a store from before it existed starts at its newest message (no sound for old mail).
  REQUIRE(saveNotifs(n));
  Notifs back;
  REQUIRE(loadNotifs(back));
  CHECK(back.heard == 1768480000);
  std::string text;
  REQUIRE(fs::readFile("/data/cache/inbox/notifs.json", text));
  const size_t at = text.find("\"heard\":1768480000");
  REQUIRE(at != std::string::npos);
  text.replace(at, 18, "\"x\":1");
  REQUIRE(fs::writeFile("/data/cache/inbox/notifs.json", text));
  REQUIRE(loadNotifs(back));
  CHECK(back.heard == 1768480000);
  // The phone removes the chat and posts it again unchanged (its notification was rebuilt): nothing new.
  applyQueue(pr.paper, n, state({{3, pr.fromPhone(3, R"({"t":"remove","k":"wa-1"})")}, {4, pr.fromPhone(4, kPostA)}}), kNow);
  CHECK(pos::inbox::freshMessages(n) == 0);
  // One more message in that chat: one.
  const std::string more = R"({"t":"post","k":"wa-1","app":"whatsapp","chat":"Family","reply":true,"ts":1768480050,"msgs":[{"from":"Bram","text":"Yes","ts":1768480000},{"from":"Anna","text":"Good","ts":1768480050}]})";
  applyQueue(pr.paper, n, state({{5, pr.fromPhone(5, more)}}), kNow);
  CHECK(pos::inbox::freshMessages(n) == 1);
  // WhatsApp is open and saw it (its seen marker): no sound for what is on the screen.
  Store whatsapp("/data/apps/whatsapp.json");
  whatsapp.set("seen", 1768480050);
  REQUIRE(whatsapp.save());
  CHECK(pos::inbox::freshMessages(n) == 0);
}
