#include "apps/inbox/relay_fetch.h"

#include <algorithm>

#include "core/agenda.h"
#include "core/clipboard.h"
#include "core/log.h"

namespace pos::inbox {

namespace {

// The UI thread may have sent envelopes (tx) while this ran: store only the new rx into the pairing as it is now.
void saveRx(const companion::Pairing& read) {
  companion::Pairing cur;
  if (!companion::loadPairing(cur) || cur.pid != read.pid) return;  // unpaired or paired again meanwhile: nothing to keep
  cur.rx = std::max(cur.rx, read.rx);
  companion::savePairing(cur);
}

}  // namespace

RelayFetch fetchRelay(HaClient& ha, int64_t now, bool takeClip) {
  RelayFetch f;
  companion::Pairing pairing;
  f.paired = companion::loadPairing(pairing);
  companion::loadNotifs(f.notifs);
  if (!f.paired) return f;
  f.http = ha.get(std::string("/api/states/") + companion::kToPaperEntity);
  if (!f.http.ok()) return f;
  f.applied = companion::applyQueue(pairing, f.notifs, f.http.body, now, takeClip);
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
  saveRx(pairing);
  companion::saveNotifs(f.notifs);
  if (!f.applied.agenda.empty()) {  // the phone's calendar (one way, D28)
    agenda::Agenda a;
    agenda::load(a);
    for (const std::string& plain : f.applied.agenda) f.agendaChanged = agenda::apply(a, plain, now) || f.agendaChanged;
    if (f.agendaChanged) agenda::save(a);
  }
  if (f.applied.hasClip) f.clipFromPhone = clipboard::set(f.applied.clip, clipboard::kFromPhone);  // never pushed back
  if (f.applied.ack) ha.callService("script", "paperos_relay_ack", companion::ackBody("to_paper", pairing.rx));  // best effort
  return f;
}

}  // namespace pos::inbox
