// Reading the companion relay (docs/COMPANION.md 8, "Paper"): GET sensor.paperos_relay_to_paper, apply the envelopes to the local
// notification store, save the pairing and the store, ack what was read. Blocking: run it on the worker (ctx.async); the inbox apps,
// the scheduled refresh and the clipboard sync share it. A `clip` from the phone replaces the Paper's clipboard here (when
// `takeClip`, i.e. Settings shares the clipboard); an `agenda` from the phone replaces the days it covers in the saved agenda (D28).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/companion.h"
#include "core/ha.h"
#include "core/os.h"

namespace pos::inbox {

struct RelayFetch {
  bool paired = false;     // false: nothing was requested
  HaResult http;           // the GET
  companion::ApplyResult applied;
  companion::Notifs notifs;  // the store after applying (also when the GET failed: what was saved before)
  bool clipFromPhone = false;  // the clipboard now holds what the phone sent
  bool agendaChanged = false;  // the phone sent its calendar: /data/cache/agenda/agenda.json is new (core/agenda.h)
};
RelayFetch fetchRelay(HaClient& ha, int64_t now, bool takeClip);

// The launcher's own look.
// Due: a phone is paired and nobody read or tried the relay in the last kLauncherCheckSec of this boot (a failed try
// counts: away from the known Wi-Fi a try costs a scan of several seconds).
constexpr int kLauncherCheckSec = 600;
bool relayCheckDue();
// Reads the relay on the worker like any other read (the store, both badges, the agenda and the clipboard follow);
// `done` runs on the UI thread, unless an app was closed meanwhile (the worker drops what was queued before).
void checkRelay(Os& os, std::function<void()> done);

}  // namespace pos::inbox
