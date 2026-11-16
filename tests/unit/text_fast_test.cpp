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
