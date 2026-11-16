// Reading the companion relay (docs/COMPANION.md 8, "Paper"): GET sensor.paperos_relay_to_paper, apply the envelopes to the local
// notification store, save the pairing and the store, ack what was read. Blocking: run it on the worker (ctx.async); the inbox apps
// and the scheduled refresh share it.
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
};
RelayFetch fetchRelay(HaClient& ha, int64_t now);

}  // namespace pos::inbox
