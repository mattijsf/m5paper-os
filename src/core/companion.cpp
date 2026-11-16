#include "core/companion.h"

#include <algorithm>

#include <ArduinoJson.h>

#include "core/fs.h"

namespace pos::companion {

namespace {

const char* kPairingPath = "/data/companion.json";
const char* kNotifsPath = "/data/cache/inbox/notifs.json";

std::string cut(const std::string& s, size_t chars) {
  size_t i = 0, n = 0;
  while (i < s.size() && n < chars) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    i += c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    ++n;
  }
  return s.substr(0, std::min(i, s.size()));
}

size_t charCount(const std::string& s) {
  size_t n = 0;
  for (char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
  return n;
}

std::string str(JsonVariantConst v, size_t chars) { return v.is<const char*>() ? cut(v.as<std::string>(), chars) : std::string(); }

int64_t num(JsonVariantConst v) { return v.is<int64_t>() ? v.as<int64_t>() : v.is<double>() ? static_cast<int64_t>(v.as<double>()) : 0; }

std::string toJson(const JsonDocument& doc) {
  std::string s;
  serializeJson(doc, s);
  return s;
}

std::string push(Pairing& p, const JsonDocument& plain) {
  const std::string text = toJson(plain);
  if (text.size() > kMaxPlain || !p.paired) return "";
  const uint64_t ctr = p.tx + 1;
  const std::string env = seal(p.session.d2p, kDirD2P, p.pid, ctr, text);
  if (env.empty()) return "";
  p.tx = ctr;
  JsonDocument body;
  body["queue"] = "to_phone";
  body["id"] = ctr;
  body["env"] = env;
  return toJson(body);
}

void applyPost(Notifs& n, JsonObjectConst o) {
  Notif x;
  x.key = str(o["k"], 120);
  x.app = str(o["app"], 16);
  if (x.key.empty() || (x.app != "whatsapp" && x.app != "mail")) return;
  x.chat = str(o["chat"], 60);
  x.reply = o["reply"].is<bool>() && o["reply"].as<bool>();
  x.ts = num(o["ts"]);
  for (JsonObjectConst m : o["msgs"].as<JsonArrayConst>()) {
    if (x.msgs.size() >= 6) break;
    NotifMsg nm;
    nm.from = str(m["from"], 80);
    nm.subject = str(m["subj"], 150);
    nm.text = str(m["text"], 500);
    nm.ts = num(m["ts"]);
    if (nm.ts <= 0) nm.ts = x.ts;
    if (nm.text.empty() && nm.subject.empty()) continue;
    x.msgs.push_back(std::move(nm));
  }
  if (x.ts <= 0 && !x.msgs.empty()) x.ts = x.msgs.back().ts;
  n.items.erase(std::remove_if(n.items.begin(), n.items.end(), [&](const Notif& i) { return i.key == x.key; }), n.items.end());
  if (!x.msgs.empty()) n.items.push_back(std::move(x));
}

}  // namespace

bool loadPairing(Pairing& out) {
  out = Pairing{};
  std::string text;
  if (!fs::readFile(kPairingPath, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  Pairing p;
  if (!unb64Fixed(doc["pid"].as<std::string>(), p.pid) || !unb64Fixed(doc["sk"].as<std::string>(), p.sk) ||
      !unb64Fixed(doc["phone_pk"].as<std::string>(), p.phonePk) || !publicKeyOf(p.sk, p.pk))
    return false;
  p.phoneName = str(doc["phone"], 40);
  p.tx = doc["tx"].as<uint64_t>();
  p.rx = doc["rx"].as<uint64_t>();
  if (!deriveSession(p.sk, p.phonePk, p.pid, p.pk, p.phonePk, p.session)) return false;
  p.paired = doc["paired"].as<bool>();
  out = p;
  return out.paired;
}

bool savePairing(const Pairing& p) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["paired"] = p.paired;
  doc["pid"] = b64(p.pid);
  doc["sk"] = b64(p.sk);
  doc["phone_pk"] = b64(p.phonePk);
  doc["phone"] = p.phoneName;
  doc["tx"] = p.tx;
  doc["rx"] = p.rx;
  return fs::writeFile(kPairingPath, toJson(doc));
}

void forgetPairing() {
  fs::removeAll(kPairingPath);
  fs::removeAll(kNotifsPath);
}

bool loadNotifs(Notifs& out) {
  out = Notifs{};
  std::string text;
  if (!fs::readFile(kNotifsPath, text)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, text)) return false;
  for (JsonObjectConst o : doc["items"].as<JsonArrayConst>()) {
    Notif x;
    x.key = o["k"].as<std::string>();
    x.app = o["app"].as<std::string>();
    x.chat = o["chat"].as<std::string>();
    x.reply = o["reply"].as<bool>();
    x.ts = num(o["ts"]);
    for (JsonObjectConst m : o["msgs"].as<JsonArrayConst>())
      x.msgs.push_back(NotifMsg{m["from"].as<std::string>(), m["subj"].as<std::string>(), m["text"].as<std::string>(), num(m["ts"])});
    if (!x.key.empty()) out.items.push_back(std::move(x));
  }
  for (JsonObjectConst o : doc["sent"].as<JsonArrayConst>()) out.sent.push_back(SentReply{o["ref"].as<uint64_t>(), o["name"].as<std::string>()});
  return true;
}

bool saveNotifs(const Notifs& n) {
  JsonDocument doc;
  JsonArray items = doc["items"].to<JsonArray>();
  for (const Notif& x : n.items) {
    JsonObject o = items.add<JsonObject>();
    o["k"] = x.key;
    o["app"] = x.app;
    o["chat"] = x.chat;
    o["reply"] = x.reply;
    o["ts"] = x.ts;
    JsonArray msgs = o["msgs"].to<JsonArray>();
    for (const NotifMsg& m : x.msgs) {
      JsonObject mo = msgs.add<JsonObject>();
      mo["from"] = m.from;
      if (!m.subject.empty()) mo["subj"] = m.subject;
      mo["text"] = m.text;
      mo["ts"] = m.ts;
    }
  }
  JsonArray sent = doc["sent"].to<JsonArray>();
  for (const SentReply& s : n.sent) {
    JsonObject o = sent.add<JsonObject>();
    o["ref"] = s.ref;
    o["name"] = s.name;
  }
  fs::mkdirs("/data/cache/inbox");
  return fs::writeFile(kNotifsPath, toJson(doc));
}

const Notif* findNotif(const Notifs& n, const std::string& key) {
  for (const Notif& x : n.items)
    if (x.key == key) return &x;
  return nullptr;
}

bool expire(Notifs& n, int64_t now) {
  if (now <= 0) return false;
  const size_t before = n.items.size();
  n.items.erase(std::remove_if(n.items.begin(), n.items.end(), [&](const Notif& x) { return x.ts > 0 && x.ts < now - kKeepSeconds; }), n.items.end());
  return n.items.size() != before;
}

std::string resultText(const std::string& why) {
  if (why.empty()) return "sent";
  if (why == "gone") return "the notification is gone from the phone";
  if (why == "stale") return "too old";
  if (why == "too_long") return "too long";
  if (why == "too_soon") return "wait 10 seconds";
  if (why == "hourly_limit") return "20 per hour reached";
  return "the phone could not send it";
}

ApplyResult applyQueue(Pairing& p, Notifs& n, const std::string& stateJson, int64_t now, bool takeClip) {
  ApplyResult r;
  JsonDocument doc;
  if (deserializeJson(doc, stateJson)) return r;
  JsonArrayConst queue = doc["attributes"]["queue"].as<JsonArrayConst>();
  if (!doc["attributes"].is<JsonObjectConst>()) return r;
  r.parsed = true;
  struct Entry {
    uint64_t id;
    std::string env;
  };
  std::vector<Entry> entries;
  for (JsonObjectConst e : queue) {
    if (!e["id"].is<uint64_t>() || !e["e"].is<const char*>()) {
      ++r.rejected;
      continue;
    }
    entries.push_back(Entry{e["id"].as<uint64_t>(), e["e"].as<std::string>()});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.id < b.id; });
  if (!p.paired) {
    r.rejected += static_cast<int>(entries.size());
    return r;
  }
  for (const Entry& e : entries) {
    if (e.id <= p.rx) {
      r.ack = true;
      continue;
    }
    Opened o;
    if (open(p.session.p2d, kDirP2D, p.pid, e.env, o) != OpenError::None || o.counter != e.id || o.counter <= p.rx) {
      ++r.rejected;
      continue;
    }
    JsonDocument m;
    if (deserializeJson(m, o.plain) || !m.is<JsonObjectConst>()) {
      ++r.rejected;
      continue;
    }
    p.rx = o.counter;
    r.ack = true;
    ++r.accepted;
    const std::string t = m["t"].as<std::string>();
    if (t == "post") {
      applyPost(n, m.as<JsonObjectConst>());
    } else if (t == "remove") {
      const std::string k = m["k"].as<std::string>();
      n.items.erase(std::remove_if(n.items.begin(), n.items.end(), [&](const Notif& x) { return x.key == k; }), n.items.end());
    } else if (t == "sync") {
      std::vector<std::string> keep;
      for (JsonVariantConst k : m["keys"].as<JsonArrayConst>()) keep.push_back(k.as<std::string>());
      n.items.erase(std::remove_if(n.items.begin(), n.items.end(),
                                   [&](const Notif& x) { return std::find(keep.begin(), keep.end(), x.key) == keep.end(); }),
                    n.items.end());
    } else if (t == "result") {
      const uint64_t ref = m["ref"].as<uint64_t>();
      for (auto it = n.sent.begin(); it != n.sent.end(); ++it) {
        if (it->ref != ref) continue;
        const bool ok = m["ok"].as<bool>();
        r.notes.push_back("Reply to " + it->name + (ok ? " sent" : " not sent: " + resultText(m["why"].as<std::string>())));
        n.sent.erase(it);
        break;
      }
    } else if (t == "clip") {
      const std::string text = str(m["text"], kMaxClipChars);
      const int64_t ts = num(m["ts"]);
      if (takeClip && !text.empty() && (now <= 0 || ts <= 0 || now - ts <= kClipMaxAge)) {
        r.hasClip = true;
        r.clip = text;
      }
    } else if (t == "unpair") {
      r.unpaired = true;
      break;
    }
  }
  expire(n, now);
  return r;
}

std::string pairedEnvelope(Pairing& p) {
  JsonDocument d;
  d["t"] = "paired";
  return push(p, d);
}

std::string replyEnvelope(Pairing& p, const std::string& key, const std::string& text, int64_t now) {
  JsonDocument d;
  d["t"] = "reply";
  d["k"] = key;
  d["text"] = cut(text, kMaxReplyChars);
  d["ts"] = now;
  return push(p, d);
}

std::string dismissEnvelope(Pairing& p, const std::vector<std::string>& keys) {
  JsonDocument d;
  d["t"] = "dismiss";
  JsonArray a = d["keys"].to<JsonArray>();
  size_t size = 30;
  for (const std::string& k : keys) {
    if (size + k.size() + 3 > kMaxPlain) break;  // the rest stay on the phone as they are
    size += k.size() + 3;
    a.add(k);
  }
  return push(p, d);
}

std::string clipEnvelope(Pairing& p, const std::string& text, int64_t now) {
  std::string t = cut(text, kMaxClipChars);
  while (!t.empty()) {
    JsonDocument d;
    d["t"] = "clip";
    d["text"] = t;
    d["ts"] = now;
    if (measureJson(d) <= kMaxPlain) return push(p, d);
    t = cut(t, charCount(t) * 3 / 4);  // escapes (quotes, line feeds) made it too long
  }
  return "";
}

std::string unpairEnvelope(Pairing& p) {
  JsonDocument d;
  d["t"] = "unpair";
  return push(p, d);
}

std::string ackBody(const std::string& queue, uint64_t upto) {
  JsonDocument d;
  d["queue"] = queue;
  d["upto"] = upto;
  return toJson(d);
}

PendingPairing startPairing() {
  PendingPairing pp;
  pp.keys = makeKeyPair();
  randomBytes(pp.pid.data(), kPidBytes);
  return pp;
}

HelloResult readHello(const PendingPairing& pending, const std::string& stateJson) {
  HelloResult r;
  JsonDocument doc;
  if (deserializeJson(doc, stateJson)) return r;
  for (JsonObjectConst e : doc["attributes"]["queue"].as<JsonArrayConst>()) {
    if (!e["e"].is<const char*>()) continue;
    Hello h;
    if (openHello(pending.keys.sk, pending.keys.pk, pending.pid, e["e"].as<std::string>(), h) != OpenError::None) continue;
    JsonDocument m;
    if (deserializeJson(m, h.plain) || m["t"] != "hello") continue;
    r.found = true;
    r.phoneName = str(m["name"], 40);
    if (r.phoneName.empty()) r.phoneName = "Phone";
    r.code = codeText(h.session.code);
    r.pairing.paired = true;
    r.pairing.pid = pending.pid;
    r.pairing.sk = pending.keys.sk;
    r.pairing.pk = pending.keys.pk;
    r.pairing.phonePk = h.phonePk;
    r.pairing.phoneName = r.phoneName;
    r.pairing.session = h.session;
    break;
  }
  return r;
}

}  // namespace pos::companion
