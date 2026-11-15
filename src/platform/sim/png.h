// Minimal grayscale PNG writer for simulator screenshots (no compression, no dependencies).
#pragma once

#include <cstdint>
#include <string>

namespace pos::sim {

// Writes an 8-bit grayscale PNG. `pixels` is w*h bytes, row-major. Returns false on I/O error.
bool writePngGray8(const std::string& path, const uint8_t* pixels, int w, int h);

}  // namespace pos::sim
