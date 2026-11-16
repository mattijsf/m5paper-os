#include "apps/inbox/clipboard_sync.h"

#include <memory>

#include "apps/inbox/relay_fetch.h"
#include "core/clipboard.h"
#include "core/companion.h"
#include "core/ha.h"
#include "core/log.h"
#include "core/paired_owed.h"

namespace pos::inbox {

namespace {

// Worth another try when Wi-Fi next comes up; a missing token or a refused request is not.
bool transient(HaError e) { return e == HaError::NoWifi || e == HaError::Timeout || e == HaError::Server || e == HaError::Other; }

// One `clip` envelope to the relay, stamped with the time of the copy (the phone drops a clip older than 24 hours by it).
void send(Os* o, const std::string& text, int64_t ts) {
  companion::Pairing p;
  if (!companion::loadPairing(p)) {
    clipboard::markSent(ts);  // unpaired: nobody to send it to
    return;
  }
  // The counter is stored before the push (COMPANION.md 5).
  const std::string body = companion::clipEnvelope(p, text, ts);
  if (body.empty() || !companion::savePairing(p)) {
    clipboard::markSent(ts);
    return;
  }
  HaClient ha = HaClient::fromNet(o->net(), o->secrets());
  o->worker().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); },
                    [ts](HaResult r) {
                      const bool retry = !r.ok() && transient(r.error);
                      POS_LOG("clip", "to the phone: %s%s", r.ok() ? "pushed" : haErrorText(r.error), retry ? ", again when Wi-Fi is up" : "");
                      if (retry) clipboard::markUnsent(ts);
                      else clipboard::markSent(ts);
                    });
}

// The `paired` envelope again while it is owed (counter 1; the relay ignores it if the first push landed).
void resendPaired(Os* o) {
  const std::string body = companion::pairedOwed(o->platform().clock.now());
  if (body.empty()) return;
  HaClient ha = HaClient::fromNet(o->net(), o->secrets());
  o->worker().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); },
                    [](HaResult r) {
                      POS_LOG("companion", "paired to the phone, again: %s", r.ok() ? "pushed" : haErrorText(r.error));
                      if (r.ok()) companion::markPairedSent();
                    });
}

}  // namespace

void installClipboardSync(Os& os) {
  Os* o = &os;
  clipboard::Sync s;
  s.active = [o] {
    if (!o->config().phone.shareClipboard) return false;
    companion::Pairing p;
    return companion::loadPairing(p);
  };
  s.push = [o](const std::string& text) { send(o, text, o->platform().clock.now()); };
  s.pull = [o](std::function<void(bool)> done) {
    HaClient ha = HaClient::fromNet(o->net(), o->secrets());
    const int64_t now = o->platform().clock.now();
    auto cb = std::make_shared<std::function<void(bool)>>(std::move(done));
    o->worker().async([ha, now]() mutable { return fetchRelay(ha, now, true); },
                      [cb](RelayFetch f) {
                        if (*cb) (*cb)(f.clipFromPhone);
                      });
  };
  clipboard::setSync(std::move(s));
  // A copy that did not reach the relay goes when Wi-Fi is next up, whatever brought it up (DECISIONS.md D23). So does a
  // `paired` the phone may not have (core/paired_owed.h): without it the phone never becomes active.
  os.net().onConnected = [o] {
    resendPaired(o);
    std::string text;
    int64_t ts = 0;
    if (!clipboard::unsent(o->platform().clock.now(), companion::kClipMaxAge, text, ts)) return;
    if (!clipboard::syncing()) {
      clipboard::markSent(ts);  // unpaired or sharing switched off since
      return;
    }
    POS_LOG("clip", "resending a copy the phone has not got");
    send(o, text, ts);
  };
}

}  // namespace pos::inbox
