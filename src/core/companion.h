// Companion protocol v1, the Paper's side (docs/COMPANION.md sections 2, 5, 6, 7, 8): the pairing state, the local store of
// phone notifications, reading the to_paper queue, and the envelopes the Paper sends. Pure logic over strings and files;
// the network calls are made by the apps (inbox, Settings > Home Assistant > Phone) with HaClient. Unit-tested.
//
// Files: /data/companion.json (the pairing: keys and counters; secret, never logged or shown) and
// /data/cache/inbox/notifs.json (the decrypted notifications; private).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/companion_crypto.h"

namespace pos::companion {

constexpr const char* kToPaperEntity = "sensor.paperos_relay_to_paper";
constexpr const char* kPairingEntity = "sensor.paperos_relay_pairing";
constexpr int64_t kKeepSeconds = 7 * 86400;   // notifications older than this are dropped on the Paper
constexpr size_t kMaxReplyChars = 900;
constexpr size_t kMaxClipChars = 900;          // a `clip` text (section 7), cut by the sender
constexpr int64_t kClipMaxAge = 24 * 3600;     // an older `clip` from the phone is not applied

struct Pairing {
  bool paired = false;
  Pid pid{};
  Key sk{}, pk{}, phonePk{};
  std::string phoneName;
  uint64_t tx = 0;  // last counter sent to the phone
  uint64_t rx = 0;  // highest counter accepted from the phone
  Session session;  // derived on load, not stored
};
bool loadPairing(Pairing& out);        // false (and not paired) when there is none or it is unreadable
bool savePairing(const Pairing& p);    // atomic
void forgetPairing();                  // deletes the pairing and the notification store

// One phone notification as the Paper keeps it (a `post`).
struct NotifMsg {
  std::string from, subject, text;
  int64_t ts = 0;
};
struct Notif {
  std::string key, app, chat;  // app: "whatsapp" or "mail"
  bool reply = false;
  int64_t ts = 0;
  std::vector<NotifMsg> msgs;  // oldest first
};
struct SentReply {
  uint64_t ref = 0;  // the counter of the reply envelope
  std::string name;  // who it went to, for the result toast
};
struct Notifs {
  std::vector<Notif> items;
  std::vector<SentReply> sent;  // replies waiting for a result (the newest 10)
};
bool loadNotifs(Notifs& out);
bool saveNotifs(const Notifs& n);
const Notif* findNotif(const Notifs& n, const std::string& key);

// Reads the body of GET /api/states/sensor.paperos_relay_to_paper and applies every acceptable envelope in counter order
// (section 5). The caller saves the pairing and the store afterwards and acks up to `pairing.rx` when `ack` is true.
struct ApplyResult {
  bool parsed = false;     // the body was a state object with a queue
  int accepted = 0, rejected = 0;
  bool ack = false;        // the queue holds entries the Paper has read (id <= rx): ack them
  bool unpaired = false;   // the phone sent `unpair`
  std::vector<std::string> notes;  // result toasts: "Reply to Anna sent", "Reply to Anna not sent: ..."
  bool hasClip = false;    // the phone sent its clipboard (the newest `clip` wins; only when `takeClip`)
  std::string clip;
};
// `takeClip` false (Settings does not share the clipboard): `clip` envelopes are read and dropped.
ApplyResult applyQueue(Pairing& pairing, Notifs& notifs, const std::string& stateJson, int64_t now, bool takeClip = true);
// Drops notifications older than kKeepSeconds; true if something went.
bool expire(Notifs& notifs, int64_t now);

// The text of a result's `why` ("" = sent).
std::string resultText(const std::string& why);

// Paper -> phone (section 7.2). Each call takes the next counter (pairing.tx + 1, stored into pairing.tx): save the pairing
// before pushing. Returns the body for script.paperos_relay_push, "" if the plaintext does not fit.
std::string pairedEnvelope(Pairing& pairing);
std::string replyEnvelope(Pairing& pairing, const std::string& key, const std::string& text, int64_t now);
std::string dismissEnvelope(Pairing& pairing, const std::vector<std::string>& keys);
std::string unpairEnvelope(Pairing& pairing);
// The Paper's clipboard for the phone: {"t":"clip","text":"...","ts":now}, the text cut to 900 characters and, if needed,
// shorter until the plaintext fits 1000 bytes. "" for an empty text.
std::string clipEnvelope(Pairing& pairing, const std::string& text, int64_t now);

// {"queue":"to_paper","upto":17}
std::string ackBody(const std::string& queue, uint64_t upto);

// Pairing in progress (section 2): the Paper's new keys and pid, then the phone's hello from the pairing queue.
struct PendingPairing {
  KeyPair keys;
  Pid pid{};
};
PendingPairing startPairing();
struct HelloResult {
  bool found = false;  // a hello for this pid opened
  std::string phoneName;
  std::string code;    // "123 456"
  Pairing pairing;     // ready to save once the user confirms (paired = true, counters 0)
};
// Looks through the body of GET /api/states/sensor.paperos_relay_pairing.
HelloResult readHello(const PendingPairing& pending, const std::string& stateJson);

}  // namespace pos::companion
