#include "apps/inbox/relay_fetch.h"

#include "core/log.h"

namespace pos::inbox {

RelayFetch fetchRelay(HaClient& ha, int64_t now) {
  RelayFetch f;
  companion::Pairing pairing;
  f.paired = companion::loadPairing(pairing);
  companion::loadNotifs(f.notifs);
  if (!f.paired) return f;
  f.http = ha.get(std::string("/api/states/") + companion::kToPaperEntity);
  if (!f.http.ok()) return f;
  f.applied = companion::applyQueue(pairing, f.notifs, f.http.body, now);
  if (!f.applied.parsed) return f;
  if (f.applied.unpaired) {
    POS_LOG("relay", "the phone unpaired");
    companion::forgetPairing();
    f.paired = false;
    f.notifs = companion::Notifs{};
    ha.callService("script", "paperos_relay_ack", companion::ackBody("to_paper", pairing.rx));
    return f;
  }
  if (f.applied.accepted > 0 || f.applied.rejected > 0) POS_LOG("relay", "accepted %d, dropped %d", f.applied.accepted, f.applied.rejected);
  companion::savePairing(pairing);
  companion::saveNotifs(f.notifs);
  if (f.applied.ack) ha.callService("script", "paperos_relay_ack", companion::ackBody("to_paper", pairing.rx));  // best effort
  return f;
}

}  // namespace pos::inbox
