#include "ui/text.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/text_clean.h"
#include "gen/fonts.h"
#include "ui/emoji.h"

namespace pos::ui {

namespace {

enum class FontId : uint8_t {
  InterRegular18, InterRegular22, InterSemiBold22, InterSemiBold28, InterSemiBold40,
  LiterataRegular24, LiterataRegular28, LiterataRegular32, LiterataRegular38, LiterataItalic28, LiterataBold28,
  Lucide32, Lucide64, None
};

const uint8_t* fontData(FontId id) {
  switch (id) {
    case FontId::InterRegular18: return gen::kInterRegular18;
    case FontId::InterRegular22: return gen::kInterRegular22;
    case FontId::InterSemiBold22: return gen::kInterSemibold22;
    case FontId::InterSemiBold28: return gen::kInterSemibold28;
    case FontId::InterSemiBold40: return gen::kInterSemibold40;
    case FontId::LiterataRegular24: return gen::kLiterataRegular24;
    case FontId::LiterataRegular28: return gen::kLiterataRegular28;
    case FontId::LiterataRegular32: return gen::kLiterataRegular32;
    case FontId::LiterataRegular38: return gen::kLiterataRegular38;
    case FontId::LiterataItalic28: return gen::kLiterataItalic28;
    case FontId::LiterataBold28: return gen::kLiterataBold28;
    case FontId::Lucide32: return gen::kLucide32;
    case FontId::Lucide64: return gen::kLucide64;
    default: return nullptr;
  }
}

FontId fontFor(Type t) {
  switch (t) {
    case Type::Caption: return FontId::InterRegular18;
    case Type::Body: return FontId::InterRegular22;
    case Type::BodyStrong: return FontId::InterSemiBold22;
    case Type::Title: return FontId::InterSemiBold28;
    case Type::Headline: return FontId::InterSemiBold40;
    case Type::ReadS: return FontId::LiterataRegular24;
    case Type::ReadM: return FontId::LiterataRegular28;
    case Type::ReadL: return FontId::LiterataRegular32;
    case Type::ReadXL: return FontId::LiterataRegular38;
    case Type::ReadItalicM: return FontId::LiterataItalic28;
    case Type::ReadBoldM: return FontId::LiterataBold28;
    default: return FontId::InterRegular22;
  }
}

// A canvas holds one runtime font; reloading parses the font tables, so remember what is loaded.
thread_local const Canvas* gLoadedOn = nullptr;  // per thread: the worker measures text on its own canvas
thread_local FontId gLoaded = FontId::None;

void useFont(Canvas& c, FontId id) {
  if (gLoadedOn == &c && gLoaded == id) return;
  c.loadFont(fontData(id), lgfx::IFont::font_type_t::ft_lvgl);
  gLoadedOn = &c;
  gLoaded = id;
}

// Pixel size of a font (the emoji drawn in it come from the nearest atlas below it); 0: no emoji (icon fonts).
int fontPx(FontId id) {
  static const uint8_t kPx[] = {18, 22, 22, 28, 40, 24, 28, 32, 38, 28, 28, 0, 0, 0};
  return kPx[static_cast<int>(id)];
}

bool hasGlyph(uint32_t cp) {  // the baked ranges (tools/gen-assets.sh RANGES)
  return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0x17F) || (cp >= 0x2010 && cp <= 0x2027) || cp == 0x20AC;
}

// Decodes one UTF-8 code point; returns the number of bytes used (>= 1). cp = 0xFFFFFFFF if invalid.
size_t decode(const std::string& s, size_t i, uint32_t& cp) {
  unsigned char b = static_cast<unsigned char>(s[i]);
  size_t need = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : 99;
  if (need == 0) {
    cp = b;
    return 1;
  }
  if (need == 99 || i + need >= s.size()) {
    cp = 0xFFFFFFFFu;
    return 1;
  }
  cp = b & (0xFF >> (need + 2));
  for (size_t k = 1; k <= need; ++k) {
    unsigned char n = static_cast<unsigned char>(s[i + k]);
    if ((n & 0xC0) != 0x80) {
      cp = 0xFFFFFFFFu;
      return 1;
    }
    cp = (cp << 6) | (n & 0x3F);
  }
  return need + 1;
}

void append(std::string& out, uint32_t cp) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

textdatum_t datum(Anchor a) {
  switch (a) {
    case Anchor::TopLeft: return textdatum_t::top_left;
    case Anchor::TopCenter: return textdatum_t::top_center;
    case Anchor::TopRight: return textdatum_t::top_right;
    case Anchor::MiddleLeft: return textdatum_t::middle_left;
    case Anchor::MiddleCenter: return textdatum_t::middle_center;
    default: return textdatum_t::middle_right;
  }
}

// Score squares (Somdoku, Woordle): the fonts have no emoji, so these are drawn as boxes of the text's height, in styles that
// stay apart on the gray panel. The text itself keeps the real characters (clipboard, sent messages).
enum class Square : uint8_t { None, Solid, Hatched, Outline };
Square squareKind(uint32_t cp) {
  switch (cp) {
    case 0x1F7E9: return Square::Solid;    // green: right
    case 0x1F7E8: return Square::Hatched;  // yellow: changed / wrong place
    case 0x2B1B:                           // black large square: not in the word
    case 0x2B1C:                           // white large square
    case 0x1F7E5:                          // red
    case 0x1F7E6:                          // blue
    case 0x1F7E7:                          // orange
      return Square::Outline;
    default: return Square::None;
  }
}

bool hasSquares(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    i += decode(s, i, cp);
    if (squareKind(cp) != Square::None) return true;
  }
  return false;
}

int squareSize(Canvas& c) { return std::max(8, static_cast<int>(c.fontHeight()) * 62 / 100); }
int squareAdvance(Canvas& c) { return squareSize(c) + std::max(2, static_cast<int>(c.fontHeight()) / 10); }

void drawSquare(Canvas& c, Square kind, int x, int top, Color color) {
  const int s = squareSize(c);
  const int y = top + (static_cast<int>(c.fontHeight()) - s) / 2;
  if (kind == Square::Solid) {
    c.fillRect(x, y, s, s, color);
    return;
  }
  c.drawRect(x, y, s, s, color);
  c.drawRect(x + 1, y + 1, s - 2, s - 2, color);
  if (kind == Square::Hatched) {
    const int step = std::max(4, s / 4);
    for (int d = step; d < 2 * s; d += step) {
      const int ax = x + std::max(0, d - s + 1), ay = y + std::min(d, s - 1);
      const int bx = x + std::min(d, s - 1), by = y + std::max(0, d - s + 1);
      c.drawLine(ax, ay, bx, by, color);
    }
  }
}

// ---- Fast text path (2026-11-16, DISPLAY.md "Tap latency")
//
// LovyanGFX draws a BFF (LVGL) glyph by mallocing and decoding its bitmap on every call and writing each pixel with its
// own 1x1 writeFillRect: 10-20 ms per line of the reader on the device, 155-361 ms per page. Here every glyph is
// rasterised once by LovyanGFX itself on a scratch canvas (black on white: the 16 alpha levels come back exactly, one
// level = 17 gray values) and kept; drawing then clones LovyanGFX's draw_string + draw_alpha_bitmap_common (datum,
// first-glyph shift, background fill from the first glyph's left to the last one's right, the same integer blend)
// writing straight into an 8-bit canvas. tests/unit/text_test.cpp checks it is byte-identical to LovyanGFX.

bool gFastText = true;
thread_local unsigned gFastDraws = 0;

inline uint8_t mix(uint32_t back, uint32_t fore, uint32_t alpha) {  // LovyanGFX's alpha_color for one channel, 4-bit alpha
  const int32_t b = static_cast<int32_t>(back), f = static_cast<int32_t>(fore);
  return static_cast<uint8_t>(b + ((f - b) * static_cast<int32_t>(alpha) + 7) / 15);
}

struct Glyph {
  bool ready = false;  // probed
  bool ok = false;     // usable by the fast path
  int16_t adv = 0;     // what drawChar advances the pen by
  int16_t xoff = 0;    // bitmap left relative to the pen (BFF bbox_x)
  int16_t w = 0;       // bitmap width (BFF bbox_w)
  int16_t yoff = 0;    // first bitmap row relative to the string's top (after the datum)
  int16_t h = 0;       // bitmap rows (0: nothing drawn, e.g. a space)
  const uint8_t* alpha = nullptr;  // w * h levels 0..15
};

constexpr int kSlots = 95 + 224 + 24 + 1;  // the baked ranges, see hasGlyph()
int slotOf(uint32_t cp) {
  if (cp >= 0x20 && cp <= 0x7E) return static_cast<int>(cp - 0x20);
  if (cp >= 0xA0 && cp <= 0x17F) return 95 + static_cast<int>(cp - 0xA0);
  if (cp >= 0x2010 && cp <= 0x2027) return 95 + 224 + static_cast<int>(cp - 0x2010);
  if (cp == 0x20AC) return kSlots - 1;
  return -1;
}

constexpr int kFonts = static_cast<int>(FontId::None);
constexpr int kProbeW = 200, kProbeH = 160, kProbeX = 50, kProbeY = 40;

// One per thread (the worker may draw text on its own canvases). Big allocations on purpose: on the device anything over
// 16 KB comes from PSRAM, so the cache does not eat the internal heap Wi-Fi and TLS need.
struct GlyphCache {
  std::vector<Glyph> table;                                  // kFonts * kSlots
  std::unordered_map<uint64_t, Glyph> other;                 // icons (private-use code points)
  std::vector<std::unique_ptr<uint8_t[]>> pages;             // alpha bitmaps
  unsigned emojiGeneration = 0;                              // emoji::generation() the emoji entries in `other` belong to
  size_t used = kPage;
  static constexpr size_t kPage = 32 * 1024;
  Canvas probe;
  bool probeReady = false;

  uint8_t* alloc(size_t n) {
    if (n > kPage) return nullptr;
    if (used + n > kPage) {
      pages.emplace_back(new uint8_t[kPage]);
      used = 0;
    }
    uint8_t* p = pages.back().get() + used;
    used += n;
    return p;
  }
};
thread_local std::unique_ptr<GlyphCache> gCache;

GlyphCache& cache() {
  if (!gCache) {
    gCache.reset(new GlyphCache());
    gCache->table.resize(static_cast<size_t>(kFonts) * kSlots);
  }
  return *gCache;
}

void probeGlyph(GlyphCache& gc, FontId id, uint32_t cp, Glyph& g) {
  g.ready = true;
  Canvas& p = gc.probe;
  if (!gc.probeReady) {
    p.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    p.setPsram(true);  // device: keep the 32 KB off the internal heap
    if (!p.createSprite(kProbeW, kProbeH)) {
      p.setPsram(false);  // host builds
      if (!p.createSprite(kProbeW, kProbeH)) return;
    }
    gc.probeReady = true;
  }
  useFont(p, id);
  const lgfx::IFont* font = p.getFont();
  lgfx::FontMetrics m{};
  font->getDefaultMetric(&m);
  if (!font->updateFontMetric(&m, static_cast<uint16_t>(cp))) return;  // no glyph: LovyanGFX draws its dummy
  // drawChar with the background fill switched off (filled_x far right) but fore != back: exactly the glyph's box is
  // written, its empty pixels as `back`. On a canvas of a gray no level produces, the true box shows (LovyanGFX writes
  // the box's empty rows too, over a neighbouring glyph: the fast path must do the same).
  constexpr uint8_t kSentinel = 128;  // not mix(255, 0, a) for any a
  p.fillScreen(static_cast<Color>(kSentinel) * 0x010101u);
  lgfx::TextStyle ts;
  ts.fore_rgb888 = 0x000000u;
  ts.back_rgb888 = 0xFFFFFFu;
  ts.size_x = ts.size_y = 1;
  int32_t filled = 1 << 30;
  g.adv = static_cast<int16_t>(font->drawChar(&p, kProbeX, kProbeY - m.y_offset, static_cast<uint16_t>(cp), &ts, &m, filled));
  g.xoff = m.x_offset;
  g.w = m.width;
  const int x0 = kProbeX + m.x_offset;
  if (x0 < 0 || x0 + g.w > kProbeW || g.w < 0) return;
  const auto* buf = static_cast<const uint8_t*>(p.getBuffer());
  int first = -1, last = -1;
  for (int y = 0; y < kProbeH; ++y)
    for (int x = 0; x < kProbeW; ++x)
      if (buf[y * kProbeW + x] != kSentinel) {
        if (x < x0 || x >= x0 + g.w) return;  // written outside the box the metrics give: not understood, keep LovyanGFX
        if (first < 0) first = y;
        last = y;
      }
  if (first == 0 || last == kProbeH - 1) return;  // may be cut by the probe canvas
  g.h = first < 0 ? 0 : static_cast<int16_t>(last - first + 1);
  g.yoff = first < 0 ? 0 : static_cast<int16_t>(first - kProbeY);
  if (g.h > 0) {
    // Black on white: level a (1..14) came out as mix(255, 0, a) (integer division truncates: level 1 is 239, not 238).
    int8_t level[256];
    std::memset(level, -1, sizeof level);
    level[255] = 0;
    for (uint32_t a = 1; a < 15; ++a) level[mix(255, 0, a)] = static_cast<int8_t>(a);
    level[0] = 15;  // full alpha is the text colour itself
    uint8_t* a = gc.alloc(static_cast<size_t>(g.w) * g.h);
    if (!a) return;
    for (int y = 0; y < g.h; ++y)
      for (int x = 0; x < g.w; ++x) {
        const int l = level[buf[(first + y) * kProbeW + x0 + x]];
        if (l < 0) return;  // a hole in the box, or another bit depth: keep LovyanGFX
        a[y * g.w + x] = static_cast<uint8_t>(l);
      }
    g.alpha = a;
  }
  g.ok = true;
}

const Glyph& glyphFor(FontId id, uint32_t cp) {
  GlyphCache& gc = cache();
  const int slot = slotOf(cp);
  Glyph* g;
  if (slot >= 0) {
    g = &gc.table[static_cast<size_t>(id) * kSlots + slot];
  } else {
    g = &gc.other[(static_cast<uint64_t>(id) << 32) | cp];
  }
  if (!g->ready) probeGlyph(gc, id, cp, *g);
  return *g;
}

// ---- Emoji (docs/UI.md "Emoji"): bitmaps from the atlas on the card (ui/emoji), kept in the same glyph cache as the letters,
// so after the first use an emoji costs what a letter costs. Placed on the font's baseline (the bottom of its "x").
bool isEmoji(uint32_t cp) { return cp >= 0x2000 && !hasGlyph(cp) && squareKind(cp) == Square::None && emoji::available(cp); }

bool hasEmoji(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    i += decode(s, i, cp);
    if (cp >= 0x2000 && isEmoji(cp)) return true;
  }
  return false;
}

int baselineOf(FontId id) {
  const Glyph& x = glyphFor(id, 'x');
  return x.ok && x.h > 0 ? x.yoff + x.h : fontPx(id) * 4 / 5;
}

const Glyph& emojiGlyph(FontId id, uint32_t cp) {
  GlyphCache& gc = cache();
  if (gc.emojiGeneration != emoji::generation()) {  // an atlas was replaced: its old bitmaps go (their memory stays in the pages)
    for (auto it = gc.other.begin(); it != gc.other.end();) it = (it->first & 0x80000000u) ? gc.other.erase(it) : std::next(it);
    gc.emojiGeneration = emoji::generation();
  }
  Glyph& g = gc.other[(static_cast<uint64_t>(id) << 32) | 0x80000000u | cp];  // apart from the font glyphs of the same code point
  if (g.ready) return g;
  g.ready = true;
  emoji::Bitmap bm;
  if (fontPx(id) == 0 || !emoji::load(emoji::atlasPx(fontPx(id)), cp, bm)) return g;
  g.adv = static_cast<int16_t>(bm.adv);
  g.xoff = static_cast<int16_t>(bm.x);
  g.w = static_cast<int16_t>(bm.w);
  g.yoff = static_cast<int16_t>(baselineOf(id) + bm.y);
  g.h = static_cast<int16_t>(bm.h);
  if (g.w > 0 && g.h > 0) {
    uint8_t* a = gc.alloc(bm.levels.size());
    if (!a) return g;
    std::memcpy(a, bm.levels.data(), bm.levels.size());
    g.alpha = a;
  } else {
    g.h = 0;
  }
  g.ok = true;
  return g;
}

// Width of sanitized text with score squares and / or emoji, the font `id` already loaded on `c`.
int widthSpecial(Canvas& c, FontId id, const std::string& s);

// Draws like c.drawString(text, x, y) with the current font, datum `d`, colours `color` on `bg`. Returns false (and draws
// nothing) when the fast path does not apply.
bool fastDraw(Canvas& c, FontId id, const std::string& text, int x, int y, textdatum_t d, Color color, Color bg) {
  if (!gFastText || c.getColorDepth() != lgfx::color_depth_t::grayscale_8bit || c.getRotation() != 0 ||
      c.getTextSizeX() != 1.0f || c.getTextSizeY() != 1.0f || c.getTextPadding() != 0 || !c.getBuffer())
    return false;
  // Collect the glyphs first: any glyph the fast path cannot handle sends the whole string to LovyanGFX.
  struct Item { const Glyph* g; };
  Item items[256];
  int n = 0;
  bool anyEmoji = false;
  for (size_t i = 0; i < text.size();) {
    uint32_t cp;
    i += decode(text, i, cp);
    if (cp < 0x20 || (cp >= 0xFE00 && cp < 0xFE10)) continue;
    if (n == 256) return false;
    const bool emo = cp >= 0x2000 && isEmoji(cp);
    if (cp > 0xFFFF && !emo) return false;
    const Glyph& g = emo ? emojiGlyph(id, cp) : glyphFor(id, cp);
    if (!g.ok) return false;
    anyEmoji = anyEmoji || emo;
    items[n++].g = &g;
  }
  const int height = c.fontHeight();
  if (d & middle_left) y -= height >> 1;
  else if (d & bottom_left) y -= height;
  else if (d & baseline_left) return false;  // not used by us
  if (d & (top_center | top_right)) {
    const int cwidth = anyEmoji ? widthSpecial(c, id, text) : c.textWidth(text.c_str());
    x -= (d & top_center) ? cwidth >> 1 : cwidth;
  }
  int sumX = n && items[0].g->xoff < 0 ? -items[0].g->xoff : 0;

  auto* cc = c.getColorConverter();
  const bool fillbg = color != bg;
  const uint32_t back = fillbg ? bg : c.getBaseColor();
  uint8_t lut[16];
  for (uint32_t a = 0; a < 16; ++a) {
    if (a == 0) { lut[a] = static_cast<uint8_t>(cc->convert(back)); continue; }
    if (a == 15) { lut[a] = static_cast<uint8_t>(cc->convert(color)); continue; }
    const uint32_t r = mix((back >> 16) & 0xFF, (color >> 16) & 0xFF, a), gg = mix((back >> 8) & 0xFF, (color >> 8) & 0xFF, a),
                   b = mix(back & 0xFF, color & 0xFF, a);
    lut[a] = static_cast<uint8_t>(cc->convert((r << 16) | (gg << 8) | b));
  }
  const uint8_t backRaw = static_cast<uint8_t>(cc->convert(fillbg ? bg : color));  // fill colour (fillbg only)

  int32_t clipX, clipY, clipW, clipH;  // int32_t is long on the ESP32: convert once
  c.getClipRect(&clipX, &clipY, &clipW, &clipH);
  const int cx = static_cast<int>(clipX), cy = static_cast<int>(clipY), cw = static_cast<int>(clipW), ch = static_cast<int>(clipH);
  if (cw <= 0 || ch <= 0) return true;
  const int clipR = cx + cw, clipB = cy + ch;
  const int stride = c.width();
  auto* buf = static_cast<uint8_t*>(c.getBuffer());
  auto fill = [&](int fx, int fy, int fw, int fh) {
    const int x0 = std::max(fx, cx), x1 = std::min(fx + fw, clipR), y0 = std::max(fy, cy), y1 = std::min(fy + fh, clipB);
    for (int yy = y0; yy < y1; ++yy)
      if (x1 > x0) std::memset(buf + yy * stride + x0, backRaw, static_cast<size_t>(x1 - x0));
  };
  int filled = 0;
  for (int i = 0; i < n; ++i) {
    const Glyph& g = *items[i].g;
    const int pen = x + sumX;
    if (fillbg) {
      const int left = std::max(filled, pen + std::min<int>(g.xoff, 0));
      const int right = pen + std::max<int>(g.w + g.xoff, g.adv);
      filled = right;
      if (left < right) {
        int top = y, bottom = y + height;
        if (g.h > 0) {
          top = std::min(top, y + g.yoff);
          bottom = std::max(bottom, y + g.yoff + g.h);
        }
        fill(left, top, right - left, bottom - top);
      }
    }
    if (g.h > 0 && g.w > 0) {
      const int gx = pen + g.xoff, gy = y + g.yoff;
      const int x0 = std::max(gx, cx), x1 = std::min(gx + g.w, clipR);
      const int y0 = std::max(gy, cy), y1 = std::min(gy + g.h, clipB);
      for (int yy = y0; yy < y1; ++yy) {
        const uint8_t* arow = g.alpha + (yy - gy) * g.w - gx;
        uint8_t* drow = buf + yy * stride;
        if (fillbg) {
          for (int xx = x0; xx < x1; ++xx) drow[xx] = lut[arow[xx]];
        } else {
          for (int xx = x0; xx < x1; ++xx)
            if (arow[xx]) drow[xx] = lut[arow[xx]];
        }
      }
    }
    sumX += g.adv;
  }
  ++gFastDraws;
  return true;
}

int widthSpecial(Canvas& c, FontId id, const std::string& s) {
  int w = 0;
  std::string run;
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    const size_t n = decode(s, i, cp);
    if (squareKind(cp) != Square::None) {
      if (!run.empty()) w += c.textWidth(run.c_str());
      run.clear();
      w += squareAdvance(c);
    } else if (cp >= 0x2000 && isEmoji(cp)) {
      if (!run.empty()) w += c.textWidth(run.c_str());
      run.clear();
      w += emojiGlyph(id, cp).adv;
    } else {
      run += s.substr(i, n);
    }
    i += n;
  }
  if (!run.empty()) w += c.textWidth(run.c_str());
  return w;
}

// The slow path's emoji (a canvas the fast path does not take): the glyph's levels blended pixel by pixel.
void drawEmojiSlow(Canvas& c, FontId id, uint32_t cp, int left, int top, Color color, Color bg) {
  const Glyph& g = emojiGlyph(id, cp);
  if (!g.ok) return;
  if (color != bg) c.fillRect(left, top, g.adv, c.fontHeight(), bg);
  for (int y = 0; y < g.h; ++y)
    for (int x = 0; x < g.w; ++x) {
      const uint32_t a = g.alpha[y * g.w + x];
      if (!a) continue;
      const uint32_t r = mix((bg >> 16) & 0xFF, (color >> 16) & 0xFF, a), gg = mix((bg >> 8) & 0xFF, (color >> 8) & 0xFF, a),
                     b = mix(bg & 0xFF, color & 0xFF, a);
      c.drawPixel(left + g.xoff + x, top + g.yoff + y, static_cast<Color>((r << 16) | (gg << 8) | b));
    }
}

}  // namespace

void setFastText(bool on) { gFastText = on; }
unsigned fastTextDraws() { return gFastDraws; }

void forgetLoadedFont() {
  gLoadedOn = nullptr;
  gLoaded = FontId::None;
}

// A family written as people joined by ZWJ ("👨‍👩‍👧"; the atlas has no family sequences): the bytes it spans if it is one
// (three or more people, or two with a child), else 0.
size_t familySpan(const std::string& s, size_t i) {
  auto person = [](uint32_t c) { return c == 0x1F468 || c == 0x1F469 || c == 0x1F9D1 || c == 0x1F466 || c == 0x1F467 || c == 0x1F9D2; };
  auto child = [](uint32_t c) { return c == 0x1F466 || c == 0x1F467 || c == 0x1F9D2; };
  int people = 0;
  bool kid = false;
  size_t end = i;
  for (size_t at = i; at < s.size();) {
    uint32_t cp;
    const size_t n = decode(s, at, cp);
    if (!person(cp)) break;
    ++people;
    kid = kid || child(cp);
    at += n;
    while (at < s.size()) {  // skin tones and selectors after a person
      uint32_t m;
      const size_t k = decode(s, at, m);
      if (!isEmojiModifier(m)) break;
      at += k;
    }
    end = at;
    uint32_t zwj;
    if (at >= s.size() || (decode(s, at, zwj), zwj != 0x200D)) break;
    at += 3;
  }
  return people >= 3 || (people == 2 && kid) ? end - i : 0;
}

std::string sanitize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    const size_t at = i;
    i += decode(s, i, cp);
    if (cp == '\n' || cp == '\t') cp = ' ';
    // A sequence (👩‍💻, 🇳🇱, a keycap) becomes its atlas id. A digit, # or * can only start a keycap: the next bytes must be
    // U+FE0F or U+20E3, so ordinary numbers cost no lookup.
    const bool keycap = (cp == '#' || cp == '*' || (cp >= '0' && cp <= '9')) && i + 3 <= s.size() &&
                        (s.compare(i, 3, "\xEF\xB8\x8F") == 0 || s.compare(i, 3, "\xE2\x83\xA3") == 0);
    if (keycap || cp >= 0x2000) {
      uint32_t id;
      if (const size_t n = emoji::matchSequence(s, at, id)) {
        append(out, id);
        i = at + n;
        continue;
      }
      if (const size_t n = cp >= 0x1F466 && cp <= 0x1F9D2 ? familySpan(s, at) : 0) {
        if (isEmoji(0x1F46A)) append(out, 0x1F46A);  // 👪
        i = at + n;
        continue;
      }
    }
    if (hasGlyph(cp) || squareKind(cp) != Square::None) {
      append(out, cp);
    } else if (cp == 0x200D) {  // a ZWJ sequence shows its first emoji: the joined one is skipped
      if (i < s.size()) i += decode(s, i, cp);
    } else if (isEmojiModifier(cp)) {
      // variation selectors, skin tones, the keycap mark, tags: nothing of their own
    } else if (isEmoji(cp)) {
      append(out, cp);
    } else if (isEmojiRange(cp)) {
      if (const char* t = emojiFallback(cp)) out += t;  // no atlas on the card: ":)" for a few, nothing for the rest
    } else {
      append(out, '?');
    }
  }
  return out;
}

void drawText(Canvas& c, const std::string& utf8, int x, int y, Type type, Color color, Anchor anchor, Color bg) {
  useFont(c, fontFor(type));
  c.setTextColor(color, bg);
  const std::string text = sanitize(utf8);
  const FontId id = fontFor(type);
  if (!hasSquares(text)) {
    if (fastDraw(c, id, text, x, y, datum(anchor), color, bg)) return;
    if (!hasEmoji(text)) {
      c.setTextDatum(datum(anchor));
      c.drawString(text.c_str(), x, y);
      return;
    }
  }
  // Text with score squares (or emoji the fast path did not take): drawn piece by piece from the left edge of the line.
  const int w = widthSpecial(c, id, text);
  int left = x;
  if (anchor == Anchor::TopCenter || anchor == Anchor::MiddleCenter) left = x - w / 2;
  if (anchor == Anchor::TopRight || anchor == Anchor::MiddleRight) left = x - w;
  const bool middle = anchor == Anchor::MiddleLeft || anchor == Anchor::MiddleCenter || anchor == Anchor::MiddleRight;
  const int top = middle ? y - static_cast<int>(c.fontHeight()) / 2 : y;
  c.setTextDatum(textdatum_t::top_left);
  std::string run;
  const auto flush = [&] {
    if (run.empty()) return;
    c.drawString(run.c_str(), left, top);
    left += c.textWidth(run.c_str());
    run.clear();
  };
  for (size_t i = 0; i < text.size();) {
    uint32_t cp;
    const size_t n = decode(text, i, cp);
    const Square kind = squareKind(cp);
    if (kind != Square::None) {
      flush();
      drawSquare(c, kind, left, top, color);
      left += squareAdvance(c);
    } else if (cp >= 0x2000 && isEmoji(cp)) {
      flush();
      drawEmojiSlow(c, id, cp, left, top, color, bg);
      left += emojiGlyph(id, cp).adv;
    } else {
      run += text.substr(i, n);
    }
    i += n;
  }
  flush();
}

int measureText(Canvas& c, const std::string& utf8, Type type) {
  useFont(c, fontFor(type));
  const std::string text = sanitize(utf8);
  return hasSquares(text) || hasEmoji(text) ? widthSpecial(c, fontFor(type), text) : c.textWidth(text.c_str());
}

int lineHeight(Canvas& c, Type type) {
  useFont(c, fontFor(type));
  return c.fontHeight();
}

std::string ellipsize(Canvas& c, const std::string& utf8, Type type, int maxWidth) {
  std::string s = sanitize(utf8);
  if (measureText(c, s, type) <= maxWidth) return s;
  // Candidate cut points are code point boundaries; the longest prefix + "…" that fits wins.
  std::string best;
  std::string prefix;
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    size_t n = decode(s, i, cp);
    std::string next = prefix + s.substr(i, n);
    if (measureText(c, next + "\xE2\x80\xA6", type) > maxWidth) break;
    prefix = next;
    i += n;
  }
  return prefix + "\xE2\x80\xA6";
}

void drawIcon(Canvas& c, Icon icon, int cx, int cy, int size, Color color, Color bg) {
  useFont(c, size >= 64 ? FontId::Lucide64 : FontId::Lucide32);
  std::string s;
  append(s, gen::kIconCodepoint[static_cast<size_t>(icon)]);
  c.setTextColor(color, bg);
  if (fastDraw(c, size >= 64 ? FontId::Lucide64 : FontId::Lucide32, s, cx, cy, textdatum_t::middle_center, color, bg)) return;
  c.setTextDatum(textdatum_t::middle_center);
  c.drawString(s.c_str(), cx, cy);
}

}  // namespace pos::ui
