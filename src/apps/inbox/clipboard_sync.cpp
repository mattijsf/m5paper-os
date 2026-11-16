#include "apps/inbox/clipboard_sync.h"

#include <memory>

#include "apps/inbox/relay_fetch.h"
#include "core/clipboard.h"
#include "core/companion.h"
#include "core/ha.h"
#include "core/log.h"

namespace pos::inbox {

void installClipboardSync(Os& os) {
  Os* o = &os;
  clipboard::Sync s;
  s.active = [o] {
    if (!o->config().phone.shareClipboard) return false;
    companion::Pairing p;
    return companion::loadPairing(p);
  };
  s.push = [o](const std::string& text) {
    companion::Pairing p;
    if (!companion::loadPairing(p)) return;
    // The counter is stored before the push (COMPANION.md 5).
    const std::string body = companion::clipEnvelope(p, text, o->platform().clock.now());
    if (body.empty() || !companion::savePairing(p)) return;
    HaClient ha = HaClient::fromNet(o->net(), o->secrets());
    o->worker().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); },
                      [](HaResult r) { POS_LOG("clip", "to the phone: %s", r.ok() ? "pushed" : haErrorText(r.error)); });
  };
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
}

}  // namespace pos::inbox
