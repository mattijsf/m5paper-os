#include "core/log.h"

#include <cstring>

namespace pos {

namespace {
constexpr int kLines = 50;
constexpr int kLineLen = 160;  // longer lines are cut in the ring (not on the sink)
char gRing[kLines][kLineLen];
unsigned gCount = 0;  // total lines recorded
}  // namespace

namespace detail {
void logRecord(const char* line) {
  char* dst = gRing[gCount % kLines];
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
    out += gRing[i % kLines];
    if (out.back() != '\n') out += '\n';
  }
  return out;
}

}  // namespace pos
