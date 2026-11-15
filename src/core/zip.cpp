#include "core/zip.h"

#include <algorithm>
#include <memory>

#include "core/fs.h"
#include "tinfl.h"

namespace pos::zip {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24); }

constexpr uint32_t kEocdSig = 0x06054b50u, kCentralSig = 0x02014b50u, kLocalSig = 0x04034b50u;
constexpr size_t kMaxEntries = 20000;

}  // namespace

uint32_t crc32(uint32_t crc, const uint8_t* data, size_t n) {
  static uint32_t table[256];
  static bool ready = false;
  if (!ready) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    ready = true;
  }
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

void Archive::close() {
  if (f_) std::fclose(f_);
  f_ = nullptr;
  entries_.clear();
}

bool Archive::open(const std::string& virtualPath) {
  close();
  error_.clear();
  const std::string real = fs::resolve(virtualPath);
  if (real.empty()) return fail("bad path");
  f_ = std::fopen(real.c_str(), "rb");
  if (!f_) return fail("cannot open file");
  if (std::fseek(f_, 0, SEEK_END) != 0) return fail("cannot read file");
  const long sz = std::ftell(f_);
  if (sz < 22) return fail("not a zip file (too small)");
  fileSize_ = static_cast<uint64_t>(sz);

  // End of central directory: the last 22 + comment bytes.
  const size_t tailLen = static_cast<size_t>(std::min<uint64_t>(fileSize_, 22 + 65535));
  std::vector<uint8_t> tail(tailLen);
  if (std::fseek(f_, static_cast<long>(fileSize_ - tailLen), SEEK_SET) != 0 || std::fread(tail.data(), 1, tailLen, f_) != tailLen) return fail("cannot read file");
  long eocd = -1;
  for (long i = static_cast<long>(tailLen) - 22; i >= 0; --i)
    if (u32(&tail[i]) == kEocdSig && i + 22 + u16(&tail[i + 20]) <= static_cast<long>(tailLen)) {
      eocd = i;
      break;
    }
  if (eocd < 0) return fail("not a zip file (no directory)");
  const uint8_t* e = &tail[eocd];
  const size_t count = u16(e + 10);
  const uint32_t cdSize = u32(e + 12), cdOff = u32(e + 16);
  if (u16(e + 4) != 0 || u16(e + 6) != 0) return fail("multi-part zip files are not supported");
  if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu) return fail("zip64 is not supported");
  if (count > kMaxEntries) return fail("too many entries");
  if (static_cast<uint64_t>(cdOff) + cdSize > fileSize_) return fail("corrupt directory");
  if (cdSize > 4u * 1024 * 1024) return fail("directory too large");

  std::vector<uint8_t> cd(cdSize);
  if (cdSize && (std::fseek(f_, static_cast<long>(cdOff), SEEK_SET) != 0 || std::fread(cd.data(), 1, cdSize, f_) != cdSize)) return fail("cannot read directory");
  size_t p = 0;
  for (size_t i = 0; i < count; ++i) {
    if (p + 46 > cd.size() || u32(&cd[p]) != kCentralSig) return fail("corrupt directory");
    Entry en;
    const uint16_t flags = u16(&cd[p + 8]);
    en.method = u16(&cd[p + 10]);
    en.crc = u32(&cd[p + 16]);
    en.compressedSize = u32(&cd[p + 20]);
    en.size = u32(&cd[p + 24]);
    const size_t nameLen = u16(&cd[p + 28]), extraLen = u16(&cd[p + 30]), commentLen = u16(&cd[p + 32]);
    en.localOffset = u32(&cd[p + 42]);
    if (p + 46 + nameLen + extraLen + commentLen > cd.size()) return fail("corrupt directory");
    en.name.assign(reinterpret_cast<const char*>(&cd[p + 46]), nameLen);
    if (flags & 1) en.method = 0xFFFF;  // encrypted: refused when extracted
    if (en.compressedSize == 0xFFFFFFFFu || en.size == 0xFFFFFFFFu || en.localOffset == 0xFFFFFFFFu) return fail("zip64 is not supported");
    if (static_cast<uint64_t>(en.localOffset) + 30 > fileSize_) return fail("corrupt directory");
    entries_.push_back(std::move(en));
    p += 46 + nameLen + extraLen + commentLen;
  }
  return true;
}

const Entry* Archive::find(const std::string& name) const {
  const std::string alt = (!name.empty() && name[0] == '/') ? name.substr(1) : name;
  for (const Entry& e : entries_)
    if (e.name == name || e.name == alt) return &e;
  return nullptr;
}

bool Archive::extract(const Entry& e, const Sink& sink, size_t maxBytes) {
  error_.clear();
  if (!f_) return fail("archive is not open");
  if (e.method != 0 && e.method != 8) return fail(e.method == 0xFFFF ? "encrypted entry" : "unsupported compression");
  if (e.size > maxBytes) return fail("entry too large");
  // The local header says where the data starts (its name and extra lengths may differ from the directory's).
  uint8_t lh[30];
  if (std::fseek(f_, static_cast<long>(e.localOffset), SEEK_SET) != 0 || std::fread(lh, 1, 30, f_) != 30 || u32(lh) != kLocalSig) return fail("corrupt entry header");
  const uint64_t dataOff = static_cast<uint64_t>(e.localOffset) + 30 + u16(lh + 26) + u16(lh + 28);
  if (dataOff + e.compressedSize > fileSize_) return fail("corrupt entry (past the end of the file)");
  if (std::fseek(f_, static_cast<long>(dataOff), SEEK_SET) != 0) return fail("cannot read entry");

  uint32_t crc = 0;
  uint64_t total = 0;
  size_t remaining = e.compressedSize;
  std::vector<uint8_t> in(4096);

  if (e.method == 0) {
    if (e.compressedSize != e.size) return fail("corrupt entry (sizes differ)");
    while (remaining > 0) {
      const size_t n = std::min(remaining, in.size());
      if (std::fread(in.data(), 1, n, f_) != n) return fail("cannot read entry");
      crc = crc32(crc, in.data(), n);
      total += n;
      remaining -= n;
      if (!sink(in.data(), n)) return fail("stopped");
    }
  } else {
    auto dec = std::make_unique<tinfl_decompressor>();  // about 11 KB: on the heap (the worker's stack is small)
    tinfl_init(dec.get());
    std::vector<uint8_t> dict(TINFL_LZ_DICT_SIZE);
    size_t inOfs = 0, inLen = 0, dictOfs = 0;
    tinfl_status status = TINFL_STATUS_NEEDS_MORE_INPUT;
    while (status == TINFL_STATUS_NEEDS_MORE_INPUT || status == TINFL_STATUS_HAS_MORE_OUTPUT) {
      if (inOfs >= inLen && remaining > 0) {
        const size_t n = std::min(remaining, in.size());
        if (std::fread(in.data(), 1, n, f_) != n) return fail("cannot read entry");
        inLen = n;
        inOfs = 0;
        remaining -= n;
      }
      size_t inN = inLen - inOfs, outN = dict.size() - dictOfs;
      const int flags = remaining > 0 ? TINFL_FLAG_HAS_MORE_INPUT : 0;
      status = tinfl_decompress(dec.get(), in.data() + inOfs, &inN, dict.data(), dict.data() + dictOfs, &outN, flags);
      inOfs += inN;
      if (outN) {
        total += outN;
        if (total > maxBytes || total > e.size) return fail("corrupt entry (too much data)");
        crc = crc32(crc, dict.data() + dictOfs, outN);
        if (!sink(dict.data() + dictOfs, outN)) return fail("stopped");
      }
      dictOfs = (dictOfs + outN) & (dict.size() - 1);
      if (status == TINFL_STATUS_NEEDS_MORE_INPUT && remaining == 0 && inOfs >= inLen) return fail("corrupt entry (truncated data)");
    }
    if (status != TINFL_STATUS_DONE) return fail("corrupt entry (bad compressed data)");
  }
  if (total != e.size) return fail("corrupt entry (wrong size)");
  if (crc != e.crc) return fail("corrupt entry (checksum mismatch)");
  return true;
}

bool Archive::read(const Entry& e, std::string& out, size_t maxBytes) {
  out.clear();
  out.reserve(std::min<size_t>(e.size, maxBytes));
  if (!extract(e, [&out](const uint8_t* d, size_t n) { out.append(reinterpret_cast<const char*>(d), n); return true; }, maxBytes)) {
    out.clear();
    return false;
  }
  return true;
}

bool Archive::read(const std::string& name, std::string& out, size_t maxBytes) {
  const Entry* e = find(name);
  if (!e) {
    out.clear();
    return fail("missing entry " + name);
  }
  return read(*e, out, maxBytes);
}

}  // namespace pos::zip
