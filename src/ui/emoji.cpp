#include "ui/emoji.h"

#include <algorithm>
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

struct Atlas {
  bool tried = false, ok = false;
  std::vector<Entry> index;
  std::unique_ptr<fs::Reader> file;
  uint64_t size = 0;
};

std::mutex gMutex;  // the atlases are shared by the UI thread and the worker
unsigned gLoads = 0;
Atlas gAtlas[kCount];

uint32_t u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

// With gMutex held.
Atlas* atlas(int px) {
  int i = 0;
  while (i < kCount && kSizes[i] != px) ++i;
  if (i == kCount) return nullptr;
  Atlas& a = gAtlas[i];
  if (a.tried) return a.ok ? &a : nullptr;
  a.tried = true;
  const std::string path = std::string(kDir) + "/emoji-" + std::to_string(px) + ".bin";
  int64_t mtime = 0;
  auto f = std::make_unique<fs::Reader>();
  uint8_t head[16];
  if (!fs::fileInfo(path, a.size, mtime) || !f->open(path) || !f->readAt(0, head, sizeof head)) return nullptr;
  const uint32_t count = u32(head + 8);
  if (std::memcmp(head, "PEMJ", 4) != 0 || head[4] != 1 || head[5] != px || count > kMaxEmoji || 16 + 16ull * count > a.size) return nullptr;
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
  a.file = std::move(f);
  a.ok = true;
  return &a;
}

const Entry* find(const Atlas& a, uint32_t cp) {
  auto it = std::lower_bound(a.index.begin(), a.index.end(), cp, [](const Entry& e, uint32_t c) { return e.cp < c; });
  return it != a.index.end() && it->cp == cp ? &*it : nullptr;
}

}  // namespace

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

unsigned loads() {
  std::lock_guard<std::mutex> lock(gMutex);
  return gLoads;
}

void reset() {
  std::lock_guard<std::mutex> lock(gMutex);
  for (Atlas& a : gAtlas) a = Atlas{};
}

}  // namespace pos::ui::emoji
