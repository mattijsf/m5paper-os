#include "ui/emoji.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

#include "core/fs.h"

namespace pos::ui::emoji {

namespace {

constexpr int kSizes[] = {18, 22, 24, 28};  // tools/gen-emoji.py SIZES
constexpr int kCount = sizeof kSizes / sizeof kSizes[0];
constexpr uint32_t kMaxEmoji = 8192;

struct Entry {
  uint32_t cp, offset;
  uint8_t w, h;
  int8_t x, y;
  uint8_t adv;
};

struct Seq {
  uint32_t id;
  std::vector<uint32_t> cps;
};

struct Atlas {
  bool tried = false, ok = false;
  std::vector<Entry> index;
  std::vector<Seq> seqs;         // sorted by code points
  std::vector<uint32_t> firsts;  // the first code points of the sequences, sorted
  size_t longest = 0;
  std::unique_ptr<fs::Reader> file;
  uint64_t size = 0;
  int64_t mtime = 0;
  unsigned changes = 0;  // fs::changes() when the file was last checked
};

std::mutex gMutex;  // the atlases are shared by the UI thread and the worker
unsigned gLoads = 0;
std::atomic<unsigned> gGeneration{0};
Atlas gAtlas[kCount];

uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

// With gMutex held.
Atlas* atlas(int px) {
  int i = 0;
  while (i < kCount && kSizes[i] != px) ++i;
  if (i == kCount) return nullptr;
  Atlas& a = gAtlas[i];
  const std::string path = std::string(kDir) + "/emoji-" + std::to_string(px) + ".bin";
  if (a.tried && a.changes != fs::changes()) {
    // Something was renamed or removed since the last look: if it was this atlas (`make emoji` while the device runs), the
    // index no longer matches the file. Drop it; the bitmaps ui/text kept go with the generation.
    a.changes = fs::changes();
    uint64_t size = 0;
    int64_t mtime = 0;
    const bool there = fs::fileInfo(path, size, mtime);
    if (there != a.ok || (there && (size != a.size || mtime != a.mtime))) {
      a = Atlas{};
      ++gGeneration;
    }
  }
  if (a.tried) return a.ok ? &a : nullptr;
  a.tried = true;
  a.changes = fs::changes();
  auto f = std::make_unique<fs::Reader>();
  uint8_t head[16];
  if (!fs::fileInfo(path, a.size, a.mtime) || !f->open(path) || !f->readAt(0, head, sizeof head)) return nullptr;
  const uint32_t count = u32(head + 8), seqAt = u32(head + 12);
  if (std::memcmp(head, "PEMJ", 4) != 0 || (head[4] != 1 && head[4] != 2) || head[5] != px || count > kMaxEmoji ||
      16 + 16ull * count > a.size)
    return nullptr;
  std::vector<uint8_t> raw(16 * static_cast<size_t>(count));
  if (!f->readAt(16, raw.data(), raw.size())) return nullptr;
  a.index.resize(count);
  for (uint32_t k = 0; k < count; ++k) {
    const uint8_t* p = raw.data() + 16 * k;
    Entry& e = a.index[k];
    e.cp = u32(p);
    e.offset = u32(p + 4);
    e.w = p[8];
    e.h = p[9];
    e.x = static_cast<int8_t>(p[10]);
    e.y = static_cast<int8_t>(p[11]);
    e.adv = p[12];
    const uint64_t bytes = (static_cast<uint64_t>(e.w) * e.h + 1) / 2;
    if (e.offset + bytes > a.size || (k > 0 && e.cp <= a.index[k - 1].cp)) {  // never trust the file
      a.index.clear();
      return nullptr;
    }
  }
  // Version 2: the sequence table (tools/gen-emoji.py).
  if (head[4] == 2 && seqAt) {
    uint8_t n4[4];
    if (seqAt + 4ull > a.size || !f->readAt(seqAt, n4, 4)) return nullptr;
    const uint32_t n = u32(n4);
    if (n > kMaxEmoji) return nullptr;
    std::vector<uint8_t> raw2(static_cast<size_t>(a.size - seqAt - 4));
    if (!raw2.empty() && !f->readAt(seqAt + 4, raw2.data(), raw2.size())) return nullptr;
    size_t at = 0;
    for (uint32_t k = 0; k < n; ++k) {
      if (at + 8 > raw2.size()) return nullptr;
      Seq q;
      q.id = u32(raw2.data() + at);
      const size_t len = raw2[at + 4];
      at += 8;
      if (len < 2 || len > 16 || at + 4 * len > raw2.size()) return nullptr;
      for (size_t j = 0; j < len; ++j) q.cps.push_back(u32(raw2.data() + at + 4 * j));
      at += 4 * len;
      a.longest = std::max(a.longest, len);
      a.firsts.push_back(q.cps[0]);
      a.seqs.push_back(std::move(q));
    }
    std::sort(a.seqs.begin(), a.seqs.end(), [](const Seq& x, const Seq& y) { return x.cps < y.cps; });
    std::sort(a.firsts.begin(), a.firsts.end());
    a.firsts.erase(std::unique(a.firsts.begin(), a.firsts.end()), a.firsts.end());
  }
  a.file = std::move(f);
  a.ok = true;
  return &a;
}

const Entry* find(const Atlas& a, uint32_t cp) {
  auto it = std::lower_bound(a.index.begin(), a.index.end(), cp, [](const Entry& e, uint32_t c) { return e.cp < c; });
  return it != a.index.end() && it->cp == cp ? &*it : nullptr;
}

// One UTF-8 code point at s[i]; returns the bytes used (>= 1), cp = 0xFFFFFFFF if invalid.
size_t decode(const std::string& s, size_t i, uint32_t& cp) {
  const unsigned char b = static_cast<unsigned char>(s[i]);
  const size_t need = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : 9;
  if (need == 9 || i + need >= s.size() + (need == 0 ? 1 : 0)) {
    cp = need == 0 ? b : 0xFFFFFFFFu;
    return 1;
  }
  cp = need == 0 ? b : b & (0xFF >> (need + 2));
  for (size_t k = 1; k <= need; ++k) {
    const unsigned char c = static_cast<unsigned char>(s[i + k]);
    if ((c & 0xC0) != 0x80) {
      cp = 0xFFFFFFFFu;
      return 1;
    }
    cp = (cp << 6) | (c & 0x3F);
  }
  return need + 1;
}

// Skipped while matching a sequence: variation selectors and skin tones.
bool ignorable(uint32_t cp) { return cp == 0xFE0E || cp == 0xFE0F || (cp >= 0x1F3FB && cp <= 0x1F3FF); }

}  // namespace

size_t matchSequence(const std::string& s, size_t i, uint32_t& id) {
  if (i >= s.size()) return 0;
  std::lock_guard<std::mutex> lock(gMutex);
  const Atlas* a = atlas(22);
  if (!a || a->seqs.empty()) return 0;
  uint32_t cp;
  size_t at = i + decode(s, i, cp);
  if (!std::binary_search(a->firsts.begin(), a->firsts.end(), cp)) return 0;
  std::vector<uint32_t> got{cp};
  std::vector<size_t> ends{at};  // the byte after each gathered code point (and the ignorables after it)
  while (got.size() < a->longest && at < s.size()) {
    size_t n = decode(s, at, cp);
    if (ignorable(cp)) {
      at += n;
      ends.back() = at;
      continue;
    }
    got.push_back(cp);
    at += n;
    ends.push_back(at);
  }
  for (size_t len = got.size(); len >= 2; --len) {
    const std::vector<uint32_t> want(got.begin(), got.begin() + static_cast<long>(len));
    auto it = std::lower_bound(a->seqs.begin(), a->seqs.end(), want, [](const Seq& q, const std::vector<uint32_t>& w) { return q.cps < w; });
    if (it != a->seqs.end() && it->cps == want) {
      id = it->id;
      return ends[len - 1] - i;
    }
  }
  return 0;
}

int atlasPx(int px) {
  int best = kSizes[0];
  for (int s : kSizes)
    if (s <= px) best = s;
  return best;
}

bool available(uint32_t cp) {
  if (cp < 0x2000) return false;
  std::lock_guard<std::mutex> lock(gMutex);
  const Atlas* a = atlas(22);
  return a && find(*a, cp);
}

bool load(int px, uint32_t cp, Bitmap& out) {
  std::lock_guard<std::mutex> lock(gMutex);
  Atlas* a = atlas(px);
  const Entry* e = a ? find(*a, cp) : nullptr;
  if (!e) return false;
  out.w = e->w;
  out.h = e->h;
  out.x = e->x;
  out.y = e->y;
  out.adv = e->adv;
  const size_t n = static_cast<size_t>(e->w) * e->h;
  std::vector<uint8_t> packed((n + 1) / 2);
  if (n && !a->file->readAt(e->offset, packed.data(), packed.size())) return false;
  ++gLoads;
  out.levels.resize(n);
  for (size_t i = 0; i < n; ++i) out.levels[i] = i % 2 == 0 ? packed[i / 2] >> 4 : packed[i / 2] & 0x0F;
  return true;
}

unsigned generation() { return gGeneration.load(); }

unsigned loads() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gLoads;
}

void reset() {
  std::lock_guard<std::mutex> lock(gMutex);
  for (Atlas& a : gAtlas) a = Atlas{};
  ++gGeneration;
}

}  // namespace pos::ui::emoji
