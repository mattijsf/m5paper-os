#include "core/log.h"

#include <cstdlib>
#include <cstring>

namespace pos {

namespace {
constexpr int kLines = 50;
constexpr int kLineLen = 160;  // longer lines are cut in the ring (not on the sink)
// Allocated on first use, not a static array: on the device a heap block lands in PSRAM (D27), a static array in the internal
// RAM Wi-Fi and TLS need (8 KB). Function-local static: initialised once even when two threads log first.
char* ring() {
  static char* r = static_cast<char*>(std::calloc(static_cast<size_t>(kLines) * kLineLen, 1));
  return r;
}
unsigned gCount = 0;  // total lines recorded
}  // namespace

namespace detail {
void logRecord(const char* line) {
  char* base = ring();
  if (!base) return;
  char* dst = base + (gCount % kLines) * kLineLen;
  std::strncpy(dst, line, kLineLen - 1);
  dst[kLineLen - 1] = '\0';
  ++gCount;
}
}  // namespace detail

std::string logTail(int maxLines) {
  unsigned n = gCount < kLines ? gCount : kLines;
  if (maxLines > 0 && static_cast<unsigned>(maxLines) < n) n = static_cast<unsigned>(maxLines);
  std::string out;
  for (unsigned i = gCount - n; i < gCount; ++i) {
    out += ring() + (i % kLines) * kLineLen;
    if (out.back() != '\n') out += '\n';
  }
  return out;
}

}  // namespace pos
