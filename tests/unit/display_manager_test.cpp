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
  f.fill(kFull, gray(4));
  f.dm.invalidateAll(Hint::Screen);
  f.dm.commit();
  REQUIRE(f.display.pushes.size() == 1);
  CHECK(f.display.pushes[0].mode == Refresh::Fast);
  CHECK(f.dm.refreshLogTail(1).find("why=screen") != std::string::npos);
  CHECK(f.dm.refreshLogTail(1).find("why=feedback") == std::string::npos);
}
