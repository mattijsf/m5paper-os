#include "apps/inbox/relay_fetch.h"

#include <algorithm>
#include <atomic>
#include <chrono>

#include "apps/inbox/scheduled_refresh.h"
#include "core/agenda.h"
#include "core/clipboard.h"
#include "core/log.h"
#include "core/sound.h"

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

namespace {
// When the relay was last read or tried, in steady-clock seconds; 0: not in this boot.
std::atomic<int64_t> gLastTry{0};

int64_t steadySeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}
}  // namespace

bool relayCheckDue() {
  const int64_t last = gLastTry.load();
  if (last != 0 && steadySeconds() - last < kLauncherCheckSec) return false;
  return companion::pairedPhone();
}

void checkRelay(Os& os, std::function<void()> done) {
  gLastTry = steadySeconds();  // from now on: not due (also while this one waits for its turn on the worker)
  HaClient ha = HaClient::fromNet(os.net(), os.secrets());
  const int64_t now = os.platform().clock.now();
  const bool clip = os.config().phone.shareClipboard;
  os.worker().async([ha, now, clip]() mutable { return fetchRelay(ha, now, clip); },
                    [done = std::move(done)](RelayFetch f) {
                      POS_LOG("relay", "launcher check: %s", !f.paired ? "not paired" : f.http.ok() ? "read" : haErrorText(f.http.error));
                      if (done) done();
                    });
}

RelayFetch fetchRelay(HaClient& ha, int64_t now, bool takeClip) {
  RelayFetch f;
  companion::Pairing pairing;
  f.paired = companion::loadPairing(pairing);
  companion::loadNotifs(f.notifs);
  if (!f.paired) return f;
  gLastTry = steadySeconds();
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
  // What is new for the sound: unread and later than anything a read brought before (a notification the phone posts
  // again, or one that was read here, is not).
  f.fresh = freshMessages(f.notifs);
  f.notifs.heard = std::max(f.notifs.heard, companion::newestMsgTs(f.notifs));
  saveRx(pairing);
  companion::saveNotifs(f.notifs);
  sound::arrived(f.fresh, now);
  // Whoever read the relay (WhatsApp, Mail, Agenda, the clipboard menu, a scheduled wake): both apps' launcher badges
  // follow. Until 2026-11-17 an app wrote only its own, right after marking everything seen: a badge never showed.
  writeBadges(f.notifs);
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
