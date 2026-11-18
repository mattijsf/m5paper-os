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
// The pairing with its session keys. Deriving them is two curve operations (about 50 ms on the device): for the
// worker, where the relay is read and envelopes are made. To ask only whether a phone is paired: pairedPhone().
bool loadPairing(Pairing& out);        // false (and not paired) when there is none or it is unreadable
// Whether a phone is paired (and its name): reads the stored pairing, derives nothing. For the UI thread.
bool pairedPhone(std::string* name = nullptr);
bool savePairing(const Pairing& p);    // atomic
void forgetPairing();                  // deletes the pairing, the notification store and the agenda

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
  // The time of the newest message a sound was considered for (core/sound.h): a message is new once. A store saved
  // before this existed loads with the time of its newest message.
  int64_t heard = 0;
};
// The time of the newest message in the store, 0 when it is empty.
int64_t newestMsgTs(const Notifs& n);
// The store holds text that is ready for the panel: a notification is cleaned once, when it arrives (cleanNotif, on
// the worker that reads the relay), not every time the inbox opens. A store written before that (no "clean" mark) is
// cleaned when it is loaded.
bool loadNotifs(Notifs& out);
bool saveNotifs(const Notifs& n);
// Every text of a notification through core/text_clean: what the fonts can show, links as host names, a mail's HTML as
// text (a chat's text is never read as HTML); names and subjects on one line. Cleaning clean text changes nothing.
void cleanNotif(Notif& n);
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
  std::vector<std::string> agenda;  // the plaintexts of the `agenda` messages, in counter order (core/agenda.h applies them)
  // The texts of the `clip` messages that are a link to read later (core/readlater.h linkFromText), in counter order:
  // whatever `takeClip` says and however old (a link shared for reading is no clipboard).
  std::vector<std::string> links;
};
// `takeClip` false (Settings does not share the clipboard): `clip` envelopes are read and dropped.
ApplyResult applyQueue(Pairing& pairing, Notifs& notifs, const std::string& stateJson, int64_t now, bool takeClip = true);
// Drops notifications older than kKeepSeconds; true if something went.
bool expire(Notifs& notifs, int64_t now);

// Why a reply was not sent, for the note the inbox shows: the text of a failed result's `why` (an unknown or missing
// reason reads "the phone could not send it").
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
