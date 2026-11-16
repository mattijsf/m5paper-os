#include "ui/text.h"

#include <algorithm>

#include "gen/fonts.h"

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

// Width of sanitized text with squares, the font already loaded.
int widthWithSquares(Canvas& c, const std::string& s) {
  int w = 0;
  std::string run;
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    const size_t n = decode(s, i, cp);
    if (squareKind(cp) != Square::None) {
      if (!run.empty()) w += c.textWidth(run.c_str());
      run.clear();
      w += squareAdvance(c);
    } else {
      run += s.substr(i, n);
    }
    i += n;
  }
  if (!run.empty()) w += c.textWidth(run.c_str());
  return w;
}

}  // namespace

void forgetLoadedFont() {
  gLoadedOn = nullptr;
  gLoaded = FontId::None;
}

std::string sanitize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    i += decode(s, i, cp);
    if (cp == '\n' || cp == '\t') cp = ' ';
    append(out, hasGlyph(cp) || squareKind(cp) != Square::None ? cp : '?');
  }
  return out;
}

void drawText(Canvas& c, const std::string& utf8, int x, int y, Type type, Color color, Anchor anchor, Color bg) {
  useFont(c, fontFor(type));
  c.setTextColor(color, bg);
  const std::string text = sanitize(utf8);
  if (!hasSquares(text)) {
    c.setTextDatum(datum(anchor));
    c.drawString(text.c_str(), x, y);
    return;
  }
  // Text with score squares: drawn piece by piece from the left edge of the line.
  const int w = widthWithSquares(c, text);
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
  return hasSquares(text) ? widthWithSquares(c, text) : c.textWidth(text.c_str());
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
  c.setTextDatum(textdatum_t::middle_center);
  c.drawString(s.c_str(), cx, cy);
}

}  // namespace pos::ui
