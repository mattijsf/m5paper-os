// CRC-32 (IEEE 802.3, the one of zip, PNG and the remote protocol).
#pragma once

#include <cstddef>
#include <cstdint>

namespace pos {

// The CRC of `data`; pass the CRC so far as `crc` to continue one: crc32(b, n, crc32(a, m)) is the CRC of a followed by b.
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

}  // namespace pos
