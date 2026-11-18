#include <cstring>
#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "ui/layout.h"
#include "ui/root.h"
#include "ui/textblock.h"

using namespace pos;
using namespace pos::ui;

namespace {

// Monospace metrics: 10 px per character (UTF-8 aware), 20 px line height, bold adds nothing.
class FakeMetrics : public TextMetrics {
 public:
  int width(const std::string& s, Type, bool, bool) override {
    int n = 0;
    for (unsigned char c : s)
      if ((c & 0xC0) != 0x80) ++n;
    return n * 10;
  }
  int lineHeight(Type t) override { return t == Type::ReadXL ? 30 : t == Type::ReadL ? 25 : 20; }
};

std::vector<Paragraph> makeText(int paragraphs, unsigned seed, bool headings = false) {
  static const char* kWords[] = {"the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog", "pride", "prejudice",
                                 "a", "is", "of", "extraordinarily", "word", "it", "truth", "universally", "acknowledged"};
  unsigned x = seed;
  auto rnd = [&] { x = x * 1664525u + 1013904223u; return x >> 8; };
  std::vector<Paragraph> out;
  for (int p = 0; p < paragraphs; ++p) {
    Paragraph para;
    if (headings && p % 7 == 0) para.heading = 1 + (p / 7) % 3;
    const int words = para.heading ? 2 + rnd() % 3 : 5 + rnd() % 60;
    std::string t;
    for (int w = 0; w < words; ++w) t += (w ? " " : "") + std::string(kWords[rnd() % 19]);
    para.runs.push_back(Run{t, false, false});
    out.push_back(para);
  }
  return out;
}

std::vector<std::string> allWords(const std::vector<Paragraph>& paras) {
  std::vector<std::string> out;
  for (auto& p : paras) {
    std::string s = plainText(p), w;
    for (char c : s) {
      if (c == ' ') {
        if (!w.empty()) out.push_back(w);
        w.clear();
      } else w += c;
    }
    if (!w.empty()) out.push_back(w);
  }
  return out;
}

// Words shown by a page, rebuilt from its segments.
std::vector<std::string> pageWords(const Page& pg) {
  std::vector<std::string> out;
  for (auto& line : pg.lines) {
    std::string w;
    for (auto& seg : line.segs) {
      for (char c : seg.text) {
        if (c == ' ') {
          if (!w.empty()) out.push_back(w);
          w.clear();
        } else w += c;
      }
    }
    if (!w.empty()) out.push_back(w);
  }
  return out;
}

}  // namespace

TEST_CASE("pagination: no line is wider than the column, nothing overflows the page") {
  FakeMetrics m;
  for (int width : {100, 230, 492}) {
    TextStyle st;
    st.width = width;
    st.height = 300;
    auto text = makeText(60, 7);
    auto starts = paginate(text, st, m);
    for (TextPos s : starts) {
      Page pg = layoutPage(text, s, st, m);
      int lastBottom = 0;
      for (auto& line : pg.lines) {
        REQUIRE_FALSE(line.segs.empty());
        const Seg& last = line.segs.back();
        CHECK(last.x + m.width(last.text, last.type, last.bold, last.italic) <= width);
        CHECK(line.y >= lastBottom);
        lastBottom = line.y + line.h;
      }
      CHECK(lastBottom <= st.height);
    }
  }
}

TEST_CASE("pagination: starts strictly increase, the last page ends at the end, nothing is lost or repeated") {
  FakeMetrics m;
  TextStyle st;
  st.width = 300;
  st.height = 400;
  auto text = makeText(80, 3, true);
  auto starts = paginate(text, st, m);
  REQUIRE(starts.size() > 3);
  CHECK(starts[0] == TextPos{0, 0});
  for (size_t i = 1; i < starts.size(); ++i) CHECK(starts[i - 1] < starts[i]);

  std::vector<std::string> shown;
  for (size_t i = 0; i < starts.size(); ++i) {
    Page pg = layoutPage(text, starts[i], st, m);
    CHECK_FALSE(pg.lines.empty());
    CHECK(pg.start == starts[i]);
    if (i + 1 < starts.size()) {
      CHECK(pg.next == starts[i + 1]);  // the next page starts where this one stopped
      CHECK_FALSE(pg.end);
    } else {
      CHECK(pg.end);
    }
    auto w = pageWords(pg);
    shown.insert(shown.end(), w.begin(), w.end());
  }
  CHECK(shown == allWords(text));
}

TEST_CASE("pagination: a position survives a change of layout") {
  FakeMetrics m;
  auto text = makeText(120, 11);
  TextStyle a;
  a.width = 492;
  a.height = 700;
  TextStyle b;
  b.width = 360;
  b.height = 500;
  b.body = Type::ReadL;
  auto sa = paginate(text, a, m);
  auto sb = paginate(text, b, m);
  REQUIRE(sa.size() > 5);
  for (size_t k : {size_t(0), size_t(3), sa.size() / 2, sa.size() - 1}) {
    TextPos pos = sa[k];
    int j = pageIndexOf(sb, pos);
    CHECK(!(pos < sb[j]));                        // the page starts at or before the position
    if (j + 1 < static_cast<int>(sb.size())) CHECK(pos < sb[j + 1]);  // and the next one starts after it
  }
}

TEST_CASE("pagination: a heading is never the last thing on a page") {
  FakeMetrics m;
  TextStyle st;
  st.width = 300;
  st.height = 260;
  auto text = makeText(200, 5, true);
  auto starts = paginate(text, st, m);
  for (TextPos s : starts) {
    Page pg = layoutPage(text, s, st, m);
    if (pg.end) continue;
    // the paragraph that begins right after this page must not be a body paragraph whose heading is on this page
    REQUIRE(pg.next.para < static_cast<int>(text.size()));
    int lastPara = pg.next.byte == 0 ? pg.next.para - 1 : pg.next.para;
    if (lastPara >= 0 && lastPara < static_cast<int>(text.size())) {
      INFO("page start " << s.para << ":" << s.byte << " next " << pg.next.para << ":" << pg.next.byte << " lines " << pg.lines.size() << " lastPara " << lastPara);
      CHECK_FALSE(text[lastPara].heading > 0);
    }
  }
}

TEST_CASE("pagination: a word wider than the column is cut, long words never loop") {
  FakeMetrics m;
  TextStyle st;
  st.width = 100;  // 10 characters
  st.height = 200;
  std::vector<Paragraph> text(1);
  text[0].runs.push_back(Run{"short " + std::string(35, 'w') + " end", false, false});
  auto starts = paginate(text, st, m);
  std::string joined;
  for (TextPos s : starts) {
    Page pg = layoutPage(text, s, st, m);
    for (auto& line : pg.lines) {
      int w = 0;
      for (auto& seg : line.segs) w = std::max(w, seg.x + m.width(seg.text, seg.type, false, false));
      CHECK(w <= 100);
      for (auto& seg : line.segs) joined += seg.text;
    }
  }
  CHECK(joined.find(std::string(35, 'w')) != std::string::npos);  // all 35 characters are shown, across lines
}

TEST_CASE("pagination: a page too small for one line still moves on") {
  FakeMetrics m;
  TextStyle st;
  st.width = 200;
  st.height = 5;  // less than one line
  auto text = makeText(4, 1);
  auto starts = paginate(text, st, m);
  CHECK(starts.size() > 1);
  for (size_t i = 1; i < starts.size(); ++i) CHECK(starts[i - 1] < starts[i]);
}

TEST_CASE("pagination: empty text is one empty page; styles split segments; first line indent") {
  FakeMetrics m;
  TextStyle st;
  std::vector<Paragraph> none;
  auto starts = paginate(none, st, m);
  REQUIRE(starts.size() == 1);
  Page empty = layoutPage(none, starts[0], st, m);
  CHECK(empty.end);
  CHECK(empty.lines.empty());

  std::vector<Paragraph> text(2);
  text[0].runs = {Run{"plain ", false, false}, Run{"bold", true, false}, Run{" and ", false, false}, Run{"italic", false, true}};
  text[1].runs = {Run{"second paragraph", false, false}};
  Page pg = layoutPage(text, TextPos{0, 0}, st, m);
  REQUIRE(pg.lines.size() >= 2);
  const Line& l0 = pg.lines[0];
  CHECK(l0.segs[0].x == 0);              // the first paragraph is not indented
  REQUIRE(l0.segs.size() == 4);
  CHECK(l0.segs[1].bold);
  CHECK(l0.segs[1].text == " bold");     // a space belongs to the segment that follows it
  CHECK(l0.segs[3].italic);
  CHECK(pg.lines[1].segs[0].x == st.indent);  // later body paragraphs are indented
}

TEST_CASE("parseMarked: the reader's compact chapter format") {
  std::string t;
  t += "\x05" "1Chapter One\n";
  t += "Plain \x01" "bold\x02 and \x03" "italic\x04 text\n";
  t += "\n";
  t += "\x05" "2Sub\r\n";
  t += "last line, no newline";
  auto p = parseMarked(t);
  REQUIRE(p.size() == 4);
  CHECK(p[0].heading == 1);
  CHECK(plainText(p[0]) == "Chapter One");
  CHECK(p[1].heading == 0);
  CHECK(plainText(p[1]) == "Plain bold and italic text");
  REQUIRE(p[1].runs.size() == 5);
  CHECK(p[1].runs[1].bold);
  CHECK(p[1].runs[3].italic);
  CHECK_FALSE(p[1].runs[4].bold);
  CHECK(p[2].heading == 2);
  CHECK(plainText(p[3]) == "last line, no newline");
}

namespace {
class FakeDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect, Refresh) override {}
  bool busy() override { return false; }
  void wait() override {}
};
class FakeClock : public hal::Clock {
 public:
  uint32_t millis() override { return 1000; }
  int64_t now() override { return 0; }
  void set(int64_t) override {}
};
}  // namespace

TEST_CASE("TextBlock: real fonts, paging, position and drawing") {
  Canvas canvas;
  canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  canvas.createSprite(kScreenW, kScreenH);
  canvas.fillScreen(kWhite);
  FakeDisplay display;
  FakeClock clock;
  DisplayManager dm(display, canvas, clock);
  Root root(canvas, dm);
  auto col = std::make_unique<Column>();
  auto* tb = col->add<TextBlock>();
  tb->flex = 1;
  root.setRoot(std::move(col));
  tb->setParagraphs(makeText(150, 21, true));

  const int pages = tb->pageCount();
  REQUIRE(pages > 5);
  CHECK(tb->pageIndex() == 0);
  CHECK(tb->prevPage() == false);
  CHECK(tb->nextPage());
  CHECK(tb->pageIndex() == 1);
  const TextPos p1 = tb->position();
  CHECK(tb->nextPage());
  CHECK(tb->prevPage());
  CHECK(tb->position() == p1);

  // a larger font re-paginates; the reading position keeps its place
  tb->goTo(TextPos{40, 0});
  const TextPos mark = tb->position();
  TextStyle big = tb->style();
  big.body = Type::ReadXL;
  tb->setStyle(big);
  CHECK(tb->pageCount() > pages);
  tb->goTo(mark);
  const Page& pg = tb->page();
  CHECK(!(mark < pg.start));  // the new page contains the old page's first line (the reading position)
  CHECK(mark < pg.next);

  // go to the end
  while (tb->nextPage()) {}
  CHECK(tb->atEnd());
  root.render(canvas, dm);  // drawing a page must work
  int dark = 0;
  const auto* px = static_cast<const uint8_t*>(canvas.getBuffer());
  for (int i = 0; i < kScreenW * kScreenH; ++i) dark += px[i] < 100;
  CHECK(dark > 500);
}

TEST_CASE("pagination: touching runs form one word (no space before punctuation in its own run)") {
  FakeMetrics m;
  TextStyle st;
  st.width = 500;
  std::vector<Paragraph> text(1);
  text[0].runs = {Run{"on a ", false, false}, Run{"single charge", false, true}, Run{".", false, false}, Run{" Next", false, false}};
  Page pg = layoutPage(text, TextPos{0, 0}, st, m);
  REQUIRE(pg.lines.size() == 1);
  std::string joined;
  for (auto& seg : pg.lines[0].segs) joined += seg.text;
  CHECK(joined == "on a single charge. Next");
  // and a word split by a style change is never broken between its pieces when it fits
  st.width = 150;  // 15 chars
  text[0].runs = {Run{"aaaaaaaaaa ", false, false}, Run{"bold", true, false}, Run{"ish", false, false}};
  pg = layoutPage(text, TextPos{0, 0}, st, m);
  REQUIRE(pg.lines.size() == 2);  // "aaaaaaaaaa" then "boldish" together on line two
  CHECK(pg.lines[1].segs.size() == 2);
  CHECK(pg.lines[1].segs[0].text == "bold");
  CHECK(pg.lines[1].segs[1].text == "ish");
}

TEST_CASE("layoutPage without lines ends every page where the full layout does") {
  FakeMetrics m;
  TextStyle st;
  st.width = 300;
  st.height = 200;
  for (unsigned seed : {1u, 7u, 42u}) {
    auto text = makeText(60, seed, true);
    TextPos pos{0, 0};
    for (int guard = 0; guard < 500; ++guard) {
      Page full = layoutPage(text, pos, st, m);
      Page lean = layoutPage(text, pos, st, m, false);
      CHECK(lean.next == full.next);
      CHECK(lean.end == full.end);
      CHECK(lean.lines.empty());
      if (full.end) break;
      pos = full.next;
    }
  }
}

TEST_CASE("soft breaks: no first-line indent and no gap, in parseMarked and in the layout") {
  auto paras = parseMarked("First line\n\x06Second line\n\x06Third line\nNew paragraph\n");
  REQUIRE(paras.size() == 4);
  CHECK_FALSE(paras[0].softBreak);
  CHECK(paras[1].softBreak);
  CHECK(paras[2].softBreak);
  CHECK_FALSE(paras[3].softBreak);
  FakeMetrics m;
  TextStyle st;
  st.width = 400;
  st.height = 600;
  Page pg = layoutPage(paras, TextPos{0, 0}, st, m);
  REQUIRE(pg.lines.size() == 4);
  const int pitch = pg.lines[1].y - pg.lines[0].y;
  CHECK(pg.lines[1].y - pg.lines[0].y == pitch);          // the lines of a block are one pitch apart
  CHECK(pg.lines[2].y - pg.lines[1].y == pitch);
  CHECK(pg.lines[3].y - pg.lines[2].y == pitch + st.paragraphGap);  // the gap comes only before a new paragraph
  CHECK(pg.lines[1].segs[0].x == 0);                     // no indent on a continued line
  CHECK(pg.lines[3].segs[0].x == st.indent);             // a real paragraph after body text is indented
}

TEST_CASE("TextBlock prefetch: a page drawn ahead is copied in, pixel for pixel what drawing it gives; changes drop it") {
  auto setup = [](Canvas& canvas, DisplayManager*& dmOut, Root*& rootOut, TextBlock*& tbOut, FakeDisplay& display, FakeClock& clock) {
    canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    canvas.createSprite(kScreenW, kScreenH);
    canvas.fillScreen(kWhite);
    dmOut = new DisplayManager(display, canvas, clock);
    rootOut = new Root(canvas, *dmOut);
    auto col = std::make_unique<Column>();
    tbOut = col->add<TextBlock>();
    tbOut->flex = 1;
    tbOut->setInsets(40, 54, 40, 0);
    rootOut->setRoot(std::move(col));
    tbOut->setParagraphs(makeText(150, 21, true));
    rootOut->render(canvas, *dmOut);
  };
  FakeDisplay d1, d2;
  FakeClock c1, c2;
  Canvas a, b;
  DisplayManager *dmA, *dmB;
  Root *rA, *rB;
  TextBlock *tA, *tB;
  setup(a, dmA, rA, tA, d1, c1);
  setup(b, dmB, rB, tB, d2, c2);
  const TextPos third = tA->pageStarts()[2];

  tA->prefetch(third);  // A: drawn ahead, then shown
  CHECK(tA->prefetched(third));
  tA->goTo(third);
  rA->render(a, *dmA);
  tB->goTo(third);  // B: drawn the usual way
  rB->render(b, *dmB);
  CHECK(std::memcmp(a.getBuffer(), b.getBuffer(), static_cast<size_t>(kScreenW) * kScreenH) == 0);

  TextStyle big = tA->style();
  big.body = Type::ReadL;
  tA->setStyle(big);
  CHECK_FALSE(tA->prefetched(third));  // a style change drops it
  delete rA;
  delete rB;
  delete dmA;
  delete dmB;
}

TEST_CASE("margin: every line of the paragraph starts further right and ends at the same edge; a byline is a heading at the body size") {
  std::vector<Paragraph> paras;
  Paragraph byline;
  byline.heading = 4;
  byline.margin = 40;
  byline.runs.push_back(Run{"someone 3 h", false, false});
  paras.push_back(byline);
  Paragraph reply;
  reply.margin = 40;
  reply.runs.push_back(Run{"a reply that is long enough to take more than one line of the narrow column it is given here", false, false});
  paras.push_back(reply);
  Paragraph plain;
  plain.runs.push_back(Run{"back at the left edge", false, false});
  paras.push_back(plain);
  FakeMetrics m;
  TextStyle st;
  st.width = 300;
  st.height = 600;
  st.indent = 0;
  Page pg = layoutPage(paras, TextPos{0, 0}, st, m);
  REQUIRE(pg.lines.size() >= 5);
  CHECK(pg.lines[0].segs[0].bold);                      // the byline: bold
  CHECK(pg.lines[0].segs[0].type == st.body);           // at the body size (headings 1 to 3 are larger)
  CHECK(pg.lines[0].h == pg.lines[1].h);
  for (size_t i = 0; i + 1 < pg.lines.size(); ++i) {
    CHECK(pg.lines[i].segs[0].x == 40);
    int right = 0;
    for (const Seg& s : pg.lines[i].segs) right = std::max(right, s.x + m.width(s.text, s.type, s.bold, s.italic));
    CHECK(right <= st.width);
  }
  CHECK(pg.lines.back().segs[0].x == 0);
  // A byline is kept with its text like a heading: last on a page, it moves to the next one.
  st.height = 100;  // a line of 27, the gaps of 10 and 18, the byline to 82: the reply's first line would end at 119
  std::vector<Paragraph> three{plain, byline, reply};
  std::vector<TextPos> starts = paginate(three, st, m);
  REQUIRE(starts.size() >= 2);
  CHECK(starts[1] == TextPos{1, 0});
}
