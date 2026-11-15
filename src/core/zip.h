// Read-only ZIP archives (EPUB files): central directory, stored and deflated entries, CRC check.
// Inflate is miniz's tinfl (lib/miniz). Never trusts the file: every offset and size is bounds-checked,
// encrypted, ZIP64 and unknown-method entries are refused, entry size is capped. Blocking file I/O:
// use on the worker. Platform independent (virtual paths, core/fs).
#pragma once

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace pos::zip {

struct Entry {
  std::string name;
  uint16_t method = 0;  // 0 stored, 8 deflate
  uint32_t crc = 0;
  uint32_t compressedSize = 0, size = 0;
  uint32_t localOffset = 0;
  bool isDir() const { return !name.empty() && name.back() == '/'; }
};

class Archive {
 public:
  Archive() = default;
  ~Archive() { close(); }
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;

  // Opens a virtual path (/sd/books/x.epub) and reads the central directory. False with error() set if the file is
  // missing or is not a usable zip.
  bool open(const std::string& virtualPath);
  void close();
  bool isOpen() const { return f_ != nullptr; }

  const std::vector<Entry>& entries() const { return entries_; }
  // Exact name; also tries the name without a leading "/". nullptr if absent.
  const Entry* find(const std::string& name) const;

  // Extracts an entry in pieces: `sink(data, n)` returns false to stop (extract then returns false with error "stopped").
  // The size and CRC are checked at the end. Entries larger than `maxBytes` (declared or actual) are refused.
  using Sink = std::function<bool(const uint8_t*, size_t)>;
  bool extract(const Entry& e, const Sink& sink, size_t maxBytes = 16u * 1024 * 1024);
  // Whole entry into memory.
  bool read(const Entry& e, std::string& out, size_t maxBytes = 8u * 1024 * 1024);
  bool read(const std::string& name, std::string& out, size_t maxBytes = 8u * 1024 * 1024);

  const std::string& error() const { return error_; }

 private:
  bool fail(const std::string& why) {
    error_ = why;
    return false;
  }
  FILE* f_ = nullptr;
  uint64_t fileSize_ = 0;
  std::vector<Entry> entries_;
  std::string error_;
};

uint32_t crc32(uint32_t crc, const uint8_t* data, size_t n);  // zip's CRC-32 (init 0, chainable)

}  // namespace pos::zip
