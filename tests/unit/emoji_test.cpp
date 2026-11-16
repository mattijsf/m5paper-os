// Emoji on the panel (ui/emoji, ui/text; docs/UI.md "Emoji"): atlases from the card, sequences, the fallback without a card,
// widths, and that an emoji is read from the card once and then drawn by the fast path like a letter.
#include <doctest.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>

#include "core/fs.h"
#include "core/gfx.h"
#include "ui/emoji.h"
#include "ui/text.h"
#include "ui/theme.h"

using namespace pos;
using namespace pos::ui;

namespace {

// A card with the atlases (a link to assets/emoji), or without.
struct Card {
  std::string dir;
  explicit Card(bool withEmoji) {
    char tmpl[] = "/tmp/pos-emoji-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    fs::mkdirs("/sd/paperos");
    if (withEmoji) {
      char cwd[1024];
      REQUIRE(getcwd(cwd, sizeof cwd));
      REQUIRE(symlink((std::string(cwd) + "/assets/emoji").c_str(), (dir + "/sd/paperos/emoji").c_str()) == 0);
    }
    emoji::reset();
  }
  ~Card() {
    emoji::reset();
    forgetLoadedFont();
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

struct Sheet {
  Canvas c;
  Sheet() {
    c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    c.createSprite(540, 120);
    c.fillScreen(kWhite);
  }
  int ink(int x0, int x1) {
    const auto* b = static_cast<const uint8_t*>(c.getBuffer());
    int n = 0;
    for (int y = 0; y < 120; ++y)
      for (int x = x0; x < x1; ++x) n += b[y * 540 + x] < 128;
    return n;
  }
};

const std::string kGrandpa = "\xF0\x9F\x91\xB4", kSunglasses = "\xF0\x9F\x98\x8E", kYellowHeart = "\xF0\x9F\x92\x9B";
const std::string kFamily = "Pap" + kGrandpa + "Mam\xF0\x9F\x91\xB5Luuk " + kSunglasses + " Sanne " + kYellowHeart;

}  // namespace

TEST_CASE("emoji: the atlases on the card have the emoji, every size, with sane bitmaps") {
  Card card(true);
  CHECK(emoji::available(0x1F474));
  CHECK(emoji::available(0x1F60E));
  CHECK(emoji::available(0x2764));
  CHECK_FALSE(emoji::available('a'));
  CHECK_FALSE(emoji::available(0x1F3FD));  // a skin tone never stands alone
  for (int px : {18, 22, 24, 28}) {
    emoji::Bitmap bm;
    REQUIRE(emoji::load(px, 0x1F600, bm));
    CHECK(bm.w > px / 2);
    CHECK(bm.w <= px + 4);
    CHECK(bm.h > px / 2);
    CHECK(bm.y < 0);  // above the baseline
    CHECK(bm.adv >= bm.w);
    CHECK(bm.levels.size() == static_cast<size_t>(bm.w * bm.h));
  }
  CHECK(emoji::atlasPx(40) == 28);
  CHECK(emoji::atlasPx(22) == 22);
  CHECK(emoji::atlasPx(12) == 18);
}

// The one code point sanitize() made of a sequence: its atlas id.
uint32_t only(const std::string& s) {
  const auto* b = reinterpret_cast<const unsigned char*>(s.data());
  if (s.size() != 4 || (b[0] & 0xF8) != 0xF0) return 0;
  return ((b[0] & 0x07u) << 18) | ((b[1] & 0x3Fu) << 12) | ((b[2] & 0x3Fu) << 6) | (b[3] & 0x3Fu);
}

TEST_CASE("emoji: sanitize keeps emoji, turns sequences into atlas ids, drops what is left of modifiers") {
  Card card(true);
  CHECK(sanitize(kFamily) == kFamily);
  CHECK(sanitize("ok \xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD") == "ok \xF0\x9F\x91\x8D");  // a skin tone on its own goes
  CHECK(sanitize("\xE2\x9D\xA4\xEF\xB8\x8F") == "\xE2\x9D\xA4");
  CHECK(sanitize("\xE4\xB8\xAD") == "?");  // not an emoji and no glyph: as before
  // Sequences from the atlas: woman technologist (also with a skin tone), heart on fire, a keycap, flags.
  const uint32_t tech = only(sanitize("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB"));
  CHECK(tech >= emoji::kSequenceBase);
  CHECK(emoji::available(tech));
  CHECK(only(sanitize("\xF0\x9F\x91\xA9\xF0\x9F\x8F\xBD\xE2\x80\x8D\xF0\x9F\x92\xBB")) == tech);
  CHECK(only(sanitize("\xE2\x9D\xA4\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x94\xA5")) >= emoji::kSequenceBase);
  CHECK(only(sanitize("#\xEF\xB8\x8F\xE2\x83\xA3")) >= emoji::kSequenceBase);
  const uint32_t nl = only(sanitize("\xF0\x9F\x87\xB3\xF0\x9F\x87\xB1"));
  CHECK(nl >= emoji::kSequenceBase);
  CHECK(only(sanitize("\xF0\x9F\x87\xA7\xF0\x9F\x87\xAA")) != nl);  // Belgium is another flag
  CHECK(only(sanitize("\xF0\x9F\x8F\xB4\xF3\xA0\x81\xA7\xF3\xA0\x81\xA2\xF3\xA0\x81\xA5\xF3\xA0\x81\xAE\xF3\xA0\x81\xA7\xF3\xA0\x81\xBF")) >=
        emoji::kSequenceBase);  // England
  CHECK(sanitize("2026 #1 *") == "2026 #1 *");  // digits, # and * stay text
  // A family (the atlas has none): the family emoji. An unknown ZWJ sequence: its first emoji.
  CHECK(sanitize("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7 x") == "\xF0\x9F\x91\xAA x");
  CHECK(sanitize("\xF0\x9F\x98\x80\xE2\x80\x8D\xF0\x9F\x8E\x82") == "\xF0\x9F\x98\x80");
}

TEST_CASE("emoji: without the atlas a few get their text form, the rest go, nothing becomes ?") {
  Card card(false);
  CHECK_FALSE(emoji::available(0x1F600));
  CHECK(sanitize("ok \xF0\x9F\x91\x8D") == "ok (+1)");
  CHECK(sanitize(kFamily) == "PapMamLuuk  Sanne ");
}

TEST_CASE("emoji: widths, ellipsis and drawing go through the fast path; each emoji is read from the card once") {
  Card card(true);
  Sheet s;
  setFastText(true);
  const unsigned loadsBefore = emoji::loads();
  const int plain = measureText(s.c, "PapMam", Type::BodyStrong);
  const int with = measureText(s.c, "Pap" + kGrandpa + "Mam", Type::BodyStrong);
  CHECK(with > plain + 15);
  CHECK(with < plain + 30);
  const std::string cut = ellipsize(s.c, kFamily, Type::BodyStrong, 200);
  CHECK(measureText(s.c, cut, Type::BodyStrong) <= 200);
  CHECK(cut.find(kGrandpa) != std::string::npos);

  const unsigned fastBefore = fastTextDraws();
  drawText(s.c, kFamily, 10, 10, Type::BodyStrong, ink, Anchor::TopLeft, kWhite);
  CHECK(fastTextDraws() == fastBefore + 1);  // the fast path took the line with its emoji
  const unsigned loadsFirst = emoji::loads() - loadsBefore;
  CHECK(loadsFirst == 4);  // grandpa, grandma, sunglasses, heart: one read each (measuring read them; drawing read nothing)
  drawText(s.c, kFamily, 10, 60, Type::BodyStrong, ink, Anchor::TopLeft, kWhite);
  CHECK(emoji::loads() - loadsBefore == loadsFirst);  // the second line comes from the glyph cache
  // The grandpa sits right after "Pap": ink there.
  const int papW = measureText(s.c, "Pap", Type::BodyStrong);
  CHECK(s.ink(10 + papW, 10 + papW + 20) > 20);

  // Centred and right-aligned text measures the emoji too.
  Sheet t;
  drawText(t.c, kSunglasses, 270, 60, Type::Title, ink, Anchor::MiddleCenter, kWhite);
  CHECK(t.ink(250, 290) > 30);
  CHECK(t.ink(0, 240) == 0);
  CHECK(t.ink(300, 540) == 0);

  // The slow path (a canvas the fast path does not take) draws the emoji as well.
  Sheet u;
  setFastText(false);
  drawText(u.c, "x" + kSunglasses, 10, 10, Type::Body, ink, Anchor::TopLeft, kWhite);
  setFastText(true);
  CHECK(u.ink(10 + measureText(u.c, "x", Type::Body), 60) > 20);
}

TEST_CASE("emoji: a cached emoji line costs about what a line of letters costs") {
  Card card(true);
  Sheet s;
  setFastText(true);
  const std::string letters = "PapXMamXLuuk X Sanne X";
  drawText(s.c, kFamily, 0, 0, Type::BodyStrong, ink, Anchor::TopLeft, kWhite);  // warm both caches
  drawText(s.c, letters, 0, 40, Type::BodyStrong, ink, Anchor::TopLeft, kWhite);
  auto time = [&](const std::string& text) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 400; ++i) drawText(s.c, text, 0, 0, Type::BodyStrong, ink, Anchor::TopLeft, kWhite);
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 400;
  };
  const double emojiUs = time(kFamily), letterUs = time(letters);
  MESSAGE("draw per line: emoji " << emojiUs << " us, letters " << letterUs << " us");
  CHECK(emojiUs < letterUs * 3 + 20);
}

TEST_CASE("emoji: an atlas replaced while in use is read again (make emoji while the device runs)") {
  Card card(false);
  char cwd[1024];
  REQUIRE(getcwd(cwd, sizeof cwd));
  const std::string src = std::string(cwd) + "/assets/emoji/";
  std::string good, other;
  {
    std::ifstream a(src + "emoji-22.bin", std::ios::binary), b(src + "emoji-18.bin", std::ios::binary);
    good.assign(std::istreambuf_iterator<char>(a), {});
    other.assign(std::istreambuf_iterator<char>(b), {});
  }
  REQUIRE(fs::writeFile("/sd/paperos/emoji/emoji-22.bin", good));
  emoji::reset();
  CHECK(emoji::available(0x1F600));
  const unsigned gen = emoji::generation();
  // Replaced by a file that is not a 22 px atlas (as a push replaces it: written aside, renamed over): no emoji now.
  REQUIRE(fs::writeFile("/sd/paperos/emoji/emoji-22.bin", other));
  CHECK_FALSE(emoji::available(0x1F600));
  CHECK(emoji::generation() > gen);
  // And the right one again.
  REQUIRE(fs::writeFile("/sd/paperos/emoji/emoji-22.bin", good));
  CHECK(emoji::available(0x1F600));
}
