// The Paper's `paired` envelope that the phone may not have yet (docs/COMPANION.md 2 step 4). The phone becomes active only
// after it reads `paired` (counter 1); the Paper pushes it once on Confirm, and a failed push used to leave the phone waiting
// until its pairing timed out. Now Confirm marks it owed, a push that reaches the relay clears the mark, and while it is owed
// the inbox pushes it again whenever Wi-Fi comes up (apps/inbox/clipboard_sync.cpp, net onConnected).
//
// File: /data/companion_paired_owed (the unix time of Confirm: one decimal number).
#pragma once

#include <cstdint>
#include <string>

namespace pos::companion {

constexpr int64_t kPairedOwedMax = 24 * 3600;  // after a day the phone has long given up: stop resending

void markPairedOwed(int64_t now);  // on Confirm, before the first push
void markPairedSent();             // the push reached the relay

// The body for script.paperos_relay_push to send `paired` again, "" when nothing is owed. Nothing is owed (and the mark goes)
// when there is no mark, it is older than kPairedOwedMax, no phone is paired, or the phone has already written to the Paper
// (rx > 0: the phone only writes once it is active, so it has `paired`). The resend has counter 1 again with a fresh nonce
// and leaves the stored counters alone; if the first push did land, the relay ignores the duplicate id.
std::string pairedOwed(int64_t now);

}  // namespace pos::companion
