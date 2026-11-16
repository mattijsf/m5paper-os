// Reading the companion relay (docs/COMPANION.md 8, "Paper"): GET sensor.paperos_relay_to_paper, apply the envelopes to the local
// notification store, save the pairing and the store, ack what was read. Blocking: run it on the worker (ctx.async); the inbox apps,
// the scheduled refresh and the clipboard sync share it. A `clip` from the phone replaces the Paper's clipboard here (when
// `takeClip`, i.e. Settings shares the clipboard).
#pragma once

#include <string>
#include <vector>

#include "core/companion.h"
#include "core/ha.h"

namespace pos::inbox {

struct RelayFetch {
  bool paired = false;     // false: nothing was requested
  HaResult http;           // the GET
  companion::ApplyResult applied;
  companion::Notifs notifs;  // the store after applying (also when the GET failed: what was saved before)
  bool clipFromPhone = false;  // the clipboard now holds what the phone sent
};
RelayFetch fetchRelay(HaClient& ha, int64_t now, bool takeClip);

}  // namespace pos::inbox
