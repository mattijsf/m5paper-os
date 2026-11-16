// MD5 (RFC 1321), only for HTTP Digest sign-in (core/file_server: macOS sends no Basic password over plain HTTP). Not for
// anything that needs a secure hash.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace pos {

// Lower-case hex of the MD5 of `data`.
std::string md5Hex(const std::string& data);

}  // namespace pos
