#include "core/paired_owed.h"

#include "core/companion.h"
#include "core/time_mark.h"

namespace pos::companion {

namespace {
const TimeMark kOwed("/data/companion_paired_owed");
}  // namespace

void markPairedOwed(int64_t now) { kOwed.set(now); }

void markPairedSent() { kOwed.clear(); }

std::string pairedOwed(int64_t now) {
  int64_t since = 0;
  if (!kOwed.get(since)) {
    kOwed.clear();  // missing or damaged
    return "";
  }
  Pairing p;
  if (now - since > kPairedOwedMax || !loadPairing(p) || p.rx > 0) {
    kOwed.clear();
    return "";
  }
  p.tx = 0;  // a copy: counter 1 again, nothing saved
  return pairedEnvelope(p);
}

}  // namespace pos::companion
