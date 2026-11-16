#include "core/paired_owed.h"

#include "core/companion.h"
#include "core/fs.h"

namespace pos::companion {

namespace {
const char* const kPath = "/data/companion_paired_owed";

bool owedSince(int64_t& ts) {
  std::string s;
  if (!fs::readFile(kPath, s) || s.empty() || s.size() > 20) return false;
  ts = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    ts = ts * 10 + (c - '0');
  }
  return true;
}
}  // namespace

void markPairedOwed(int64_t now) { fs::writeFile(kPath, std::to_string(now < 0 ? 0 : now)); }

void markPairedSent() { fs::removeAll(kPath); }

std::string pairedOwed(int64_t now) {
  int64_t since = 0;
  if (!owedSince(since)) {
    fs::removeAll(kPath);  // missing or damaged
    return "";
  }
  Pairing p;
  if (now - since > kPairedOwedMax ||!loadPairing(p) || p.rx > 0) {
    fs::removeAll(kPath);
    return "";
  }
  p.tx = 0;  // a copy: counter 1 again, nothing saved
  return pairedEnvelope(p);
}

}  // namespace pos::companion
