#include <doctest.h>

#include <string>
#include <vector>

#include "core/display_manager.h"

using namespace pos;

namespace {

struct Push {
  Rect rect;
  Refresh mode;
};

class FakeDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect r, Refresh m) override { pushes.push_back({r, m}); }
  bool busy() override { return isBusy; }
  void wait() override {}
  std::vector<Push> pushes;
  bool isBusy = false;
};

class FakeClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return 0; }
  void set(int64_t) override {}
  uint32_t ms = 1000;
};

struct Fixture {
  Canvas canvas;
  FakeDisplay display;
  FakeClock clock;
  DisplayManager dm;

  static Canvas& prepared(Canvas& c) {
    c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    c.createSprite(kScreenW, kScreenH);
    c.fillScreen(kWhite);
    return c;
  }
  explicit Fixture(DisplayParams p = {}) : dm(display, prepared(canvas), clock, p) {
    dm.forceClean();  // boot: panel and shadow are known and white
    display.pushes.clear();
  }
  void fill(Rect r, Color c) { canvas.fillRect(r.x, r.y, r.w, r.h, c); }
  int count(Refresh m) const {
    int n = 0;
    for (const auto& p : display.pushes) n += p.mode == m;
    return n;
  }
};

const Rect kFull{0, 0, kScreenW, kScreenH};

}  // namespace

TEST_CASE("boot forceClean is one full-screen Quality push") {
  Canvas canvas;
  FakeDisplay d;
  FakeClock c;
  DisplayManager dm(d, Fixture::prepared(canvas), c);
  dm.forceClean();
  REQUIRE(d.pushes.size() == 1);
  CHECK(d.pushes[0].mode == Refresh::Quality);
  CHECK(d.pushes[0].rect.w == kScreenW);
  CHECK(d.pushes[0].rect.h == kScreenH);
}

TEST_CASE("unchanged canvas pushes nothing") {
  Fixture f;
  f.dm.invalidate(kFull, Hint::Screen);
  f.dm.invalidate(Rect{10, 10, 50, 50}, Hint::Content);
  f.dm.commit();
  CHECK(f.display.pushes.empty());
  CHECK_FALSE(f.dm.dirty());
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);
}

TEST_CASE("sub-level anti-aliasing noise causes no update") {
  Fixture f;
  f.fill(Rect{0, 0, 100, 100}, 0xF8F8F8u);  // 248: still level 15 after quantising
  f.dm.invalidate(kFull, Hint::Screen);
  f.dm.commit();
  CHECK(f.display.pushes.empty());
}

TEST_CASE("a 10x10 change inside a full-screen invalidation pushes about 10x10") {
  Fixture f;
  f.fill(Rect{200, 300, 10, 10}, kBlack);
  f.dm.invalidate(kFull, Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Fast);
  CHECK(f.display.pushes[0].rect.x == 200);
  CHECK(f.display.pushes[0].rect.y == 300);
  CHECK(f.display.pushes[0].rect.w == 10);
  CHECK(f.display.pushes[0].rect.h == 10);
}

TEST_CASE("Feedback pushes Fast immediately, even while the panel is busy") {
  Fixture f;
  f.display.isBusy = true;
  f.fill(Rect{100, 100, 40, 40}, kBlack);
  f.fill(Rect{300, 500, 40, 40}, kBlack);
  f.dm.invalidate(Rect{100, 100, 40, 40}, Hint::Feedback);
  f.dm.invalidate(Rect{300, 500, 40, 40}, Hint::Content);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);  // only the Feedback rect; Content waits for the panel
  CHECK(f.display.pushes[0].mode == Refresh::Fast);
  CHECK(f.dm.dirty());
  CHECK(f.dm.nextDeadlineMs() == 0);
  f.display.isBusy = false;
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 2);
  CHECK(f.display.pushes[1].rect.x == 300);
}

TEST_CASE("Feedback on pure black/white needs no settle pass; with grays it gets one") {
  Fixture f;
  f.fill(Rect{100, 100, 40, 40}, kBlack);
  f.dm.invalidate(Rect{100, 100, 40, 40}, Hint::Feedback);
  f.dm.commit();
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);

  f.fill(Rect{300, 500, 40, 40}, gray(7));
  f.dm.invalidate(Rect{300, 500, 40, 40}, Hint::Feedback);
  f.dm.commit();
  CHECK(f.dm.nextDeadlineMs() == f.clock.ms + 600);
}

TEST_CASE("Feedback and Content: changes that are rows apart are pushed apart (a key and its letter in the field), close ones as one") {
  Fixture f;
  // A key at the bottom goes back to gray, its letter appears in the field at the top: two small pushes, two small settles.
  const Rect key{84, 747, 48, 66}, letter{40, 187, 16, 32};
  f.fill(key, gray(13));
  f.fill(letter, gray(5));
  f.dm.invalidate(Rect{81, 744, 54, 72}, Hint::Feedback);
  f.dm.invalidate(Rect{24, 160, 492, 72}, Hint::Feedback);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 2);
  for (const auto& p : f.display.pushes) {
    CHECK(p.mode == Refresh::Fast);
    CHECK(p.rect.h <= 66);
  }
  f.dm.tick(f.clock.ms + 600);  // one settle per tick
  f.dm.tick(f.clock.ms + 600);
  REQUIRE(f.display.pushes.size() == 4);
  CHECK(f.display.pushes[2].mode == Refresh::Text);
  CHECK(f.display.pushes[2].rect.h <= 66);
  CHECK(f.display.pushes[3].rect.h <= 66);

  // Two keys side by side (they share an edge): one push.
  Fixture g;
  g.fill(Rect{84, 747, 48, 66}, kBlack);
  g.fill(Rect{138, 747, 48, 66}, kBlack);
  g.dm.invalidate(Rect{81, 744, 54, 72}, Hint::Feedback);
  g.dm.invalidate(Rect{135, 744, 54, 72}, Hint::Feedback);
  g.dm.commit();
  REQUIRE(g.display.pushes.size() == 1);
  CHECK(g.display.pushes[0].rect.w == 102);

  // A third rect that joins two others makes one of the three.
  Fixture j;
  j.fill(Rect{10, 10, 20, 20}, kBlack);
  j.fill(Rect{110, 10, 20, 20}, kBlack);
  j.dm.invalidate(Rect{10, 10, 20, 20}, Hint::Feedback);
  j.dm.invalidate(Rect{110, 10, 20, 20}, Hint::Feedback);
  j.dm.invalidate(Rect{30, 10, 80, 20}, Hint::Feedback);
  j.dm.commit();
  REQUIRE(j.display.pushes.size() == 1);
  CHECK(j.display.pushes[0].rect.w == 120);

  // Content the same way (a status bar icon and a label far below it), each band with its own settle.
  Fixture c;
  c.fill(Rect{400, 11, 30, 30}, gray(5));
  c.fill(Rect{230, 900, 190, 20}, gray(5));
  c.dm.invalidate(Rect{0, 0, 540, 56}, Hint::Content);
  c.dm.invalidate(Rect{24, 880, 492, 60}, Hint::Content);
  c.dm.commit();
  REQUIRE(c.display.pushes.size() == 2);
  CHECK(c.display.pushes[0].rect.h == 30);
  CHECK(c.display.pushes[1].rect.h == 20);
  c.dm.tick(c.clock.ms + 600);
  c.dm.tick(c.clock.ms + 600);
  REQUIRE(c.display.pushes.size() == 4);
  CHECK(c.display.pushes[2].rect.h <= 30);
  CHECK(c.display.pushes[3].rect.h <= 30);

  // A screen change stays one pass, whatever is between its parts.
  Fixture sc;
  sc.fill(Rect{400, 11, 30, 30}, kBlack);
  sc.fill(Rect{230, 900, 190, 20}, kBlack);
  sc.dm.invalidateAll(Hint::Screen);
  sc.dm.commit();
  REQUIRE(sc.display.pushes.size() == 1);
  CHECK(sc.display.pushes[0].rect.h == 909);

  // Many scattered changes: no more pushes than the limit allows (the last band takes the rest).
  Fixture h;
  for (int16_t i = 0; i < 8; ++i) {
    const Rect r{static_cast<int16_t>(10 + i * 60), static_cast<int16_t>(100 + i * 100), 20, 20};
    h.fill(r, kBlack);
    h.dm.invalidate(r, Hint::Feedback);
  }
  h.dm.commit();
  CHECK(h.display.pushes.size() == 4);
  int covered = 0;  // every changed square is inside a pushed rect
  for (int16_t i = 0; i < 8; ++i)
    for (const auto& p : h.display.pushes)
      if (10 + i * 60 >= p.rect.x && 30 + i * 60 <= p.rect.x + p.rect.w && 100 + i * 100 >= p.rect.y && 120 + i * 100 <= p.rect.y + p.rect.h) {
        ++covered;
        break;
      }
  CHECK(covered == 8);
}

TEST_CASE("Content pushes Fast, then Text after the settle delay") {
  Fixture f;
  f.fill(Rect{50, 60, 200, 100}, gray(5));
  f.dm.invalidate(Rect{50, 60, 200, 100}, Hint::Content);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Fast);
  const uint32_t due = f.dm.nextDeadlineMs();
  CHECK(due == f.clock.ms + 600);

  f.dm.tick(due - 1);
  CHECK(f.display.pushes.size() == 1);
  f.dm.tick(due);
  REQUIRE(f.display.pushes.size() == 2);
  CHECK(f.display.pushes[1].mode == Refresh::Text);
  CHECK(f.display.pushes[1].rect.x == 50);
  CHECK(f.display.pushes[1].rect.w == 200);
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);
}

TEST_CASE("a change in the region before the delay restarts the settle timer") {
  Fixture f;
  f.fill(Rect{50, 60, 200, 100}, gray(5));
  f.dm.invalidate(Rect{50, 60, 200, 100}, Hint::Content);
  f.dm.commit();
  f.clock.ms += 400;  // still inside the 600 ms window
  f.fill(Rect{60, 70, 100, 50}, gray(9));
  f.dm.invalidate(Rect{60, 70, 100, 50}, Hint::Content);
  f.dm.commit();
  CHECK(f.count(Refresh::Fast) == 2);
  CHECK(f.dm.nextDeadlineMs() == f.clock.ms + 600);  // restarted from the second change
  f.dm.tick(1000 + 600);                             // the old due time passes: nothing happens
  CHECK(f.count(Refresh::Text) == 0);
  f.dm.tick(f.dm.nextDeadlineMs());
  CHECK(f.count(Refresh::Text) == 1);
}

TEST_CASE("an unrelated region keeps its own settle timer") {
  Fixture f;
  f.fill(Rect{0, 0, 100, 100}, gray(5));
  f.dm.invalidate(Rect{0, 0, 100, 100}, Hint::Content);
  f.dm.commit();
  const uint32_t first = f.dm.nextDeadlineMs();
  f.clock.ms += 300;
  f.fill(Rect{400, 800, 100, 100}, gray(5));
  f.dm.invalidate(Rect{400, 800, 100, 100}, Hint::Content);
  f.dm.commit();
  CHECK(f.dm.nextDeadlineMs() == first);
}

TEST_CASE("ghost budget: the next Screen hint becomes Quality, which resets the score") {
  DisplayParams p;
  p.ghostLimit = 1.0f;
  Fixture f(p);
  f.fill(kFull, kBlack);
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();  // Fast over the full screen adds 1.0
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Fast);
  CHECK(f.dm.ghostScore() == doctest::Approx(1.0f));

  f.fill(kFull, kWhite);
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 2);
  CHECK(f.display.pushes[1].mode == Refresh::Quality);
  CHECK(f.display.pushes[1].rect.w == kScreenW);
  CHECK(f.dm.ghostScore() == doctest::Approx(0.0f));
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);  // Quality dropped the settle timers

  f.fill(kFull, kBlack);
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  CHECK(f.display.pushes[2].mode == Refresh::Fast);  // back to Fast
}

TEST_CASE("Page: Fast without a settle pass; every Nth page is one full-screen Quality pass") {
  DisplayParams p;
  p.pageCleanEvery = 3;
  Fixture f(p);
  const Rect page{40, 54, 458, 883};  // the reader's text area
  for (int i = 1; i <= 7; ++i) {
    f.fill(page, i % 2 ? gray(4) : kWhite);  // grays: a Content change would get a settle pass
    f.dm.invalidate(page, Hint::Page);
    f.dm.commit();
    const auto& last = f.display.pushes.back();
    if (i % 3 == 0) {
      CHECK(last.mode == Refresh::Quality);
      CHECK(last.rect.w == kScreenW);
      CHECK(last.rect.h == kScreenH);
      CHECK(f.dm.ghostScore() == doctest::Approx(0.0f));
      CHECK(f.dm.pagesSinceClean() == 0);
    } else {
      CHECK(last.mode == Refresh::Fast);
      CHECK(f.dm.pagesSinceClean() == i % 3);
    }
    CHECK(f.dm.nextDeadlineMs() == kNoDeadline);  // never a settle pass after a page
  }
  f.dm.tick(f.clock.ms + 5000);
  CHECK(f.count(Refresh::Text) == 0);
  CHECK(f.count(Refresh::Quality) == 2);
}

TEST_CASE("Page: a page turn drops an older settle in its area; 0 switches the clean off; any full clean restarts the count") {
  DisplayParams p;
  p.pageCleanEvery = 2;
  Fixture f(p);
  f.fill(Rect{0, 900, kScreenW, 40}, gray(6));  // a footer with grays: Content, settles later
  f.dm.invalidate(Rect{0, 900, kScreenW, 40}, Hint::Content);
  f.fill(Rect{40, 54, 458, 800}, kBlack);
  f.dm.invalidate(Rect{40, 54, 458, 800}, Hint::Page);
  f.dm.commit();
  CHECK(f.dm.nextDeadlineMs() != kNoDeadline);  // the footer's settle stays (outside the page)
  CHECK(f.dm.pagesSinceClean() == 1);
  f.dm.forceClean();
  CHECK(f.dm.pagesSinceClean() == 0);

  p.pageCleanEvery = 0;
  f.dm.setParams(p);
  for (int i = 0; i < 6; ++i) {
    f.fill(kFull, i % 2 ? kBlack : kWhite);
    f.dm.invalidateAll(Hint::Page);
    f.dm.commit();
  }
  CHECK(f.count(Refresh::Quality) == 1);  // only the forceClean
}

TEST_CASE("a settle pass gives back half of the score its region added") {
  Fixture f;
  f.fill(kFull, gray(5));
  f.dm.invalidateAll(Hint::Content);
  f.dm.commit();
  CHECK(f.dm.ghostScore() == doctest::Approx(1.0f));
  f.dm.tick(f.dm.nextDeadlineMs());
  CHECK(f.dm.ghostScore() == doctest::Approx(0.5f));
}

TEST_CASE("Image hint pushes Quality on the image rect only, no settle") {
  Fixture f;
  f.fill(Rect{100, 200, 300, 300}, gray(8));
  f.dm.invalidate(Rect{0, 0, kScreenW, 600}, Hint::Image);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Quality);
  CHECK(f.display.pushes[0].rect.w == 300);
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);
}

TEST_CASE("idle clean when the score reached twice the limit") {
  DisplayParams p;
  p.ghostLimit = 0.5f;
  Fixture f(p);
  for (int i = 0; i < 2; ++i) {  // two full-screen Fast changes: score 2.0 >= 2 x 0.5 (Quality only fires on Screen)
    f.fill(kFull, i % 2 ? kWhite : kBlack);
    f.dm.invalidateAll(Hint::Content);
    f.dm.commit();
  }
  CHECK(f.dm.ghostScore() >= 1.0f);
  const uint32_t t = f.clock.ms;
  f.dm.tick(t + 599);  // settles not due yet, not idle long enough
  CHECK(f.count(Refresh::Quality) == 0);
  f.dm.tick(t + 600);  // the settle pass runs (score is still >= 2 x limit)
  CHECK(f.count(Refresh::Text) == 1);
  f.clock.ms = t + 600;
  f.dm.tick(t + 600 + 2000);  // idle for 2 s
  CHECK(f.count(Refresh::Quality) == 1);
  CHECK(f.dm.ghostScore() == doctest::Approx(0.0f));
}

TEST_CASE("refresh log text lists the pushes") {
  Fixture f;
  f.fill(Rect{200, 300, 10, 10}, kBlack);
  f.dm.invalidate(kFull, Hint::Screen);
  f.dm.commit();
  std::string log = f.dm.refreshLogTail(5);
  CHECK(log.find("mode=F rect=200,300,10,10 changed=100 why=screen") != std::string::npos);
}

TEST_CASE("diff: only the changed span of each row counts; unchanged rows cost nothing") {
  Fixture f;
  f.fill(Rect{100, 50, 30, 4}, kBlack);    // rows 50..53, x 100..129
  f.fill(Rect{300, 700, 5, 1}, kBlack);    // a far away single row
  f.dm.invalidate(kFull, Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  const Rect r = f.display.pushes[0].rect;
  CHECK(r.x == 100);
  CHECK(r.y == 50);
  CHECK(r.x + r.w == 305);
  CHECK(r.y + r.h == 701);
  // changed = 4 rows x 30 + 1 row x 5
  CHECK(f.dm.refreshLogTail(1).find("changed=125") != std::string::npos);
}

TEST_CASE("diff: noise at the ends of a span is trimmed, a changed pixel inside is kept") {
  Fixture f;
  f.fill(Rect{10, 10, 40, 1}, 0xF8F8F8u);  // 248 still level 15: no change at all
  f.fill(Rect{20, 10, 1, 1}, kBlack);      // one real change in the middle
  f.dm.invalidate(kFull, Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].rect.x == 20);
  CHECK(f.display.pushes[0].rect.w == 1);
}

TEST_CASE("navigation: a screen change right after a Down pushes one Screen pass, no Feedback first") {
  Fixture f;
  // the pressed look is not queued on Down (see Pressable); the tap then replaces the screen
  f.fill(kFull, kBlack);
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Fast);
  CHECK(f.dm.refreshLogTail(1).find("why=screen") != std::string::npos);
  CHECK(f.dm.refreshLogTail(1).find("why=feedback") == std::string::npos);
}

TEST_CASE("Screen with flat gray areas (icon wells): one Text pass, no settle; anti-aliased edges and lines stay Fast then settle") {
  Fixture f;
  // A launcher-like screen: black frames, gray(13) wells (flat areas), a 1 px gray line.
  for (int i = 0; i < 6; ++i) {
    const int x = 40 + (i % 2) * 250, y = 170 + (i / 2) * 220;
    f.canvas.drawRect(x, y, 230, 190, kBlack);
    f.canvas.fillCircle(x + 115, y + 70, 44, gray(13));
  }
  f.fill(Rect{0, 56, kScreenW, 1}, gray(9));
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Text);
  CHECK(f.dm.refreshLogTail(1).find("why=screen-gray") != std::string::npos);
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);  // nothing left to settle
  f.dm.tick(f.clock.ms + 5000);
  CHECK(f.display.pushes.size() == 1);
  const float textScore = f.dm.ghostScore();
  CHECK(textScore > 0.0f);

  // The same wells again, only the frames change: the gray is unchanged, so Fast then settle as before.
  f.fill(Rect{40, 170, 230, 1}, kWhite);
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 2);
  CHECK(f.display.pushes[1].mode == Refresh::Fast);

  // Thin gray lines and small gray specks (anti-aliased text) are no area: Fast, then a Text settle.
  Fixture g;
  for (int16_t y = 100; y < 900; y += 20) g.fill(Rect{20, y, 500, 1}, gray(9));
  for (int i = 0; i < 200; ++i) g.fill(Rect{static_cast<int16_t>(20 + (i * 37) % 480), static_cast<int16_t>(105 + (i * 53) % 780), 2, 2}, gray(6));
  g.dm.invalidateAll(Hint::Screen);
  g.dm.commit();
  REQUIRE(g.display.pushes.size() == 1);
  CHECK(g.display.pushes[0].mode == Refresh::Fast);
  g.dm.tick(g.clock.ms + 600);
  REQUIRE(g.display.pushes.size() == 2);
  CHECK(g.display.pushes[1].mode == Refresh::Text);

  // grayAreaMin 0 switches it off.
  DisplayParams p;
  p.grayAreaMin = 0;
  Fixture h(p);
  h.fill(Rect{100, 100, 200, 200}, gray(13));
  h.dm.invalidateAll(Hint::Screen);
  h.dm.commit();
  REQUIRE(h.display.pushes.size() == 1);
  CHECK(h.display.pushes[0].mode == Refresh::Fast);
}

TEST_CASE("Screen with a gray area: a settle inside the pushed box is dropped, the ghost budget's Quality still wins") {
  DisplayParams p;
  p.ghostLimit = 1.0f;
  Fixture f(p);
  f.fill(Rect{100, 100, 50, 50}, gray(5));
  f.dm.invalidate(Rect{100, 100, 50, 50}, Hint::Content);
  f.dm.commit();  // Fast + a settle pending
  CHECK(f.dm.nextDeadlineMs() != kNoDeadline);
  f.fill(Rect{50, 50, 300, 300}, gray(13));
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  CHECK(f.display.pushes.back().mode == Refresh::Text);
  CHECK(f.dm.nextDeadlineMs() == kNoDeadline);

  // A settle that sticks out of the Text pass by two columns (the screen's first frame was wider): only those columns
  // are settled later, not the whole region a second time.
  Fixture g;
  g.fill(Rect{24, 77, 494, 400}, gray(5));
  g.dm.invalidate(Rect{24, 77, 494, 400}, Hint::Content);
  g.dm.commit();
  g.fill(Rect{24, 11, 492, 500}, gray(13));
  g.dm.invalidateAll(Hint::Screen);
  g.dm.commit();
  REQUIRE(g.display.pushes.size() == 2);
  CHECK(g.display.pushes[1].mode == Refresh::Text);
  g.dm.tick(g.clock.ms + 5000);
  REQUIRE(g.display.pushes.size() == 3);
  CHECK(g.display.pushes[2].mode == Refresh::Text);
  CHECK(g.display.pushes[2].rect.x >= 516);
  CHECK(g.display.pushes[2].rect.w <= 2);

  f.fill(kFull, kBlack);  // over the limit next
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  f.fill(Rect{50, 50, 300, 300}, gray(13));
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  CHECK(f.display.pushes.back().mode == Refresh::Quality);
}
