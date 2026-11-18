// The fast text path (ui/text.cpp "Fast text path") must draw exactly what LovyanGFX draws: same pixels, byte for byte.
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/gfx.h"
#include "ui/text.h"

using namespace pos;
using namespace pos::ui;

namespace {

struct Pair {
  Canvas fast, ref;
  unsigned fastUsed = 0;
  Pair(int w, int h) {
    for (Canvas* c : {&fast, &ref}) {
      c->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
      c->createSprite(w, h);
      // A background with structure: a fill that ignores `bg` or blends against the canvas would show.
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) c->drawPixel(x, y, gray(((x / 7) + (y / 5)) % 16));
    }
  }
  ~Pair() { forgetLoadedFont(); }
  template <class F>
  void draw(F f) {
    setFastText(true);
    const unsigned before = fastTextDraws();
    f(fast);
    fastUsed = fastTextDraws() - before;
    forgetLoadedFont();  // the next canvas loads its font itself
    setFastText(false);
    f(ref);
    forgetLoadedFont();
    setFastText(true);
  }
  int differing() const {
    const auto* a = static_cast<const uint8_t*>(const_cast<Canvas&>(fast).getBuffer());
    const auto* b = static_cast<const uint8_t*>(const_cast<Canvas&>(ref).getBuffer());
    const size_t n = static_cast<size_t>(const_cast<Canvas&>(fast).width()) * const_cast<Canvas&>(fast).height();
    int d = 0;
    for (size_t i = 0; i < n; ++i) d += a[i] != b[i];
    return d;
  }
};

const std::vector<std::string> kSamples = {
    "Hello, world!",
    "\"Repeat, 'You are old, Father William,'\" said the Caterpillar.",
    "caf\xC3\xA9 cr\xC3\xA8me \xE2\x80\x94 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x82\xAC 12,50 \xC3\x89\xC3\x85\xC5\x93",
    "fjord AVAWAY To Ty iiiil",
    "  leading and trailing  ",
    "",
    "1 / 359",
};

}  // namespace

TEST_CASE("fast text: byte-identical to LovyanGFX for every type, anchor and colour pair") {
  const Type types[] = {Type::Caption, Type::Body, Type::BodyStrong, Type::Title, Type::Headline, Type::ReadS,
                        Type::ReadM, Type::ReadL, Type::ReadXL, Type::ReadItalicM, Type::ReadBoldM};
  const Anchor anchors[] = {Anchor::TopLeft, Anchor::TopCenter, Anchor::TopRight, Anchor::MiddleLeft, Anchor::MiddleCenter,
                            Anchor::MiddleRight};
  struct Colours { Color ink, bg; };
  const Colours colours[] = {{kBlack, kWhite}, {kWhite, kBlack}, {gray(3), kWhite}, {kBlack, gray(13)},
                             {kBlack, kBlack} /* the fake-bold second strike: no fill */};
  for (Type t : types) {
    for (const auto& s : kSamples) {
      Pair p(540, 140);
      int k = 0;
      p.draw([&](Canvas& c) {
        k = 0;
        for (Anchor a : anchors)
          for (const auto& col : colours) {
            const int x = (k * 53) % 400 + (a == Anchor::TopLeft || a == Anchor::MiddleLeft ? 0 : 200);
            const int y = 20 + (k * 17) % 90;
            drawText(c, s, x, y, t, col.ink, a, col.bg);
            ++k;
          }
      });
      INFO("type " << static_cast<int>(t) << " text '" << s << "'");
      CHECK(p.differing() == 0);
      if (!s.empty()) CHECK(p.fastUsed == 30);  // every string took the fast path
    }
  }
}

TEST_CASE("fast text: clipping and text running off the canvas edges") {
  Pair p(300, 120);
  p.draw([](Canvas& c) {
    drawText(c, "Off the left edge", -37, 10, Type::ReadM, kBlack, Anchor::TopLeft, kWhite);
    drawText(c, "Off the right edge and more", 200, 40, Type::Title, kBlack, Anchor::TopLeft, gray(14));
    drawText(c, "Bottom", 100, 110, Type::Headline, kWhite, Anchor::TopLeft, kBlack);
    drawText(c, "Top", 150, -12, Type::ReadL, kBlack, Anchor::TopLeft, kWhite);
    c.setClipRect(40, 30, 120, 30);
    drawText(c, "Clipped inside a box of text", 10, 25, Type::ReadM, kBlack, Anchor::TopLeft, kWhite);
    drawText(c, "Clipped centre", 100, 45, Type::Body, kWhite, Anchor::MiddleCenter, gray(4));
    c.clearClipRect();
  });
  CHECK(p.differing() == 0);
  CHECK(p.fastUsed == 6);
}

TEST_CASE("fast text: icons") {
  Pair p(400, 140);
  p.draw([](Canvas& c) {
    for (int i = 0; i < 6; ++i) {
      drawIcon(c, static_cast<Icon>(i), 40 + i * 60, 40, 32, kBlack, kWhite);
      drawIcon(c, static_cast<Icon>(i), 40 + i * 60, 100, 64, kWhite, gray(2));
    }
  });
  CHECK(p.differing() == 0);
  CHECK(p.fastUsed == 12);
}

TEST_CASE("fast text: fake-bold double strike over glyphs whose boxes overlap (reader headings)") {
  const Type types[] = {Type::ReadS, Type::ReadM, Type::ReadL, Type::ReadXL, Type::ReadItalicM, Type::ReadBoldM, Type::Headline};
  for (Type t : types) {
    Pair p(540, 120);
    p.draw([&](Canvas& c) {
      int y = 5;
      for (const char* s : {"Afterword", "fffjjj AVAWAY", "Chapter VII. A Mad Tea-Party"}) {
        drawText(c, s, 20, y, t, kBlack, Anchor::TopLeft, kWhite);
        drawText(c, s, 21, y, t, kBlack, Anchor::TopLeft, kBlack);  // TextBlock's second strike: no background fill
        y += 38;
      }
    });
    INFO("type " << static_cast<int>(t));
    CHECK(p.differing() == 0);
    CHECK(p.fastUsed == 6);
  }
}

TEST_CASE("fast text: measuring from the glyph cache gives LovyanGFX's widths, for every character and face") {
  Canvas c;
  c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  c.createSprite(64, 64);
  forgetLoadedFont();
  // Every code point of the baked ranges (tools/gen-assets.sh), alone and between letters, and the samples.
  std::vector<std::string> texts = kSamples;
  texts.push_back("jfj");                                   // a first glyph that starts left of the pen
  texts.push_back("The quick brown fox jumps over the lazy dog 0123456789 (f) [j] {y} f");
  texts.push_back("tab\there\nand a line feed, a control \x01 byte");
  auto add = [&texts](uint32_t cp) {
    std::string ch;
    if (cp < 0x80) ch += static_cast<char>(cp);
    else if (cp < 0x800) { ch += static_cast<char>(0xC0 | (cp >> 6)); ch += static_cast<char>(0x80 | (cp & 0x3F)); }
    else { ch += static_cast<char>(0xE0 | (cp >> 12)); ch += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); ch += static_cast<char>(0x80 | (cp & 0x3F)); }
    texts.push_back(ch);
    texts.push_back("a" + ch + "f" + ch);
  };
  for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) add(cp);
  for (uint32_t cp = 0xA0; cp <= 0x17F; ++cp) add(cp);
  for (uint32_t cp = 0x2010; cp <= 0x2027; ++cp) add(cp);
  add(0x20AC);
  add(0x4E2D);  // no glyph: a question mark after sanitize
  int checked = 0;
  for (int t = 0; t < static_cast<int>(Type::Count); ++t) {
    const Type type = static_cast<Type>(t);
    for (const std::string& s : texts) {
      setFastText(true);
      const int fast = measureText(c, s, type);
      setFastText(false);
      const int ref = measureText(c, s, type);
      if (fast != ref) {
        INFO("type " << t << " text '" << s << "'");
        CHECK(fast == ref);
      }
      ++checked;
    }
  }
  setFastText(true);
  forgetLoadedFont();
  CHECK(checked > 8000);
}

TEST_CASE("ellipsize: the longest beginning that fits with its ellipsis, found by halving") {
  Canvas c;
  c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  c.createSprite(64, 64);
  forgetLoadedFont();
  const std::string dots = "\xE2\x80\xA6";
  const std::vector<std::string> lines = {
      "512 points \xC2\xB7 203 comments \xC2\xB7 12 h \xC2\xB7 status.example.com.and.a.long.host.name.example.org",
      "caf\xC3\xA9 cr\xC3\xA8me \xE2\x80\x94 \xE2\x80\x9Cquoted\xE2\x80\x9D \xE2\x82\xAC 12,50 and then some more words to cut",
      "iiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii WWWWWWWWWWWWWWW",
      "short"};
  for (Type type : {Type::Caption, Type::Body, Type::BodyStrong, Type::Title, Type::ReadS}) {
    for (const std::string& s : lines) {
      for (int width : {0, 20, 77, 150, 233, 452, 492}) {
        const std::string got = ellipsize(c, s, type, width);
        // What the plain way gives: add characters while the beginning with its ellipsis still fits.
        std::string want = s;
        if (measureText(c, s, type) > width) {
          std::string prefix;
          for (size_t i = 0; i < s.size();) {
            size_t n = 1;
            while (i + n < s.size() && (static_cast<unsigned char>(s[i + n]) & 0xC0) == 0x80) ++n;
            if (measureText(c, prefix + s.substr(i, n) + dots, type) > width) break;
            prefix += s.substr(i, n);
            i += n;
          }
          want = prefix + dots;
        }
        INFO("type " << static_cast<int>(type) << " width " << width << " '" << s << "'");
        CHECK(got == want);
      }
    }
  }
  forgetLoadedFont();
}
