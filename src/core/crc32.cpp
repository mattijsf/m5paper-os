#include "core/crc32.h"

namespace pos {

namespace {
struct Table {
  uint32_t v[256];
  Table() {
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      v[n] = c;
    }
  }
};
}  // namespace

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc) {
  static const Table table;  // built on first use (thread-safe: the worker and the UI thread both call this)
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = table.v[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

}  // namespace pos
