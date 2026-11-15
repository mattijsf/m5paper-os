#include <doctest.h>

#include <vector>

#include "core/gesture.h"

using namespace pos;
using T = hal::TouchEvent;

namespace {

hal::TouchEvent ev(T::Type t, int x, int y, uint32_t ms) {
  hal::TouchEvent e;
  e.type = t;
  e.x = static_cast<int16_t>(x);
  e.y = static_cast<int16_t>(y);
  e.ms = ms;
  return e;
}

std::vector<Gesture::Type> types(const std::vector<Gesture>& v) {
  std::vector<Gesture::Type> t;
  for (auto& g : v) t.push_back(g.type);
  return t;
}

using V = std::vector<Gesture::Type>;

}  // namespace

TEST_CASE("gesture: tap") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 100, 200, 1000), out);
  r.feed(ev(T::Up, 100, 200, 1150), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::Tap});
  CHECK(out[1].x == 100);
  CHECK(out[1].y == 200);
  CHECK_FALSE(r.touching());
}

TEST_CASE("gesture: a little jitter is still a tap, 24 px is not") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 100, 100, 0), out);
  r.feed(ev(T::Move, 115, 112, 20), out);  // 19 px
  r.feed(ev(T::Up, 115, 112, 100), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::Tap});

  out.clear();
  r.feed(ev(T::Down, 100, 100, 0), out);
  r.feed(ev(T::Move, 124, 100, 20), out);  // exactly 24 px
  r.feed(ev(T::Up, 124, 100, 100), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::Cancel});
}

TEST_CASE("gesture: long press fires while the finger is down, no tap after") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 50, 60, 1000), out);
  CHECK(r.nextDeadlineMs() == 1600);
  r.tick(1599, out);
  CHECK(types(out) == V{Gesture::Down});
  r.tick(1600, out);
  CHECK(types(out) == V{Gesture::Down, Gesture::LongPress});
  CHECK(out[1].x == 50);
  CHECK(r.nextDeadlineMs() == 0xFFFFFFFFu);
  r.tick(2000, out);  // fires once
  r.feed(ev(T::Up, 50, 60, 2100), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::LongPress});
}

TEST_CASE("gesture: a held touch released late (no tick) is a long press, not a tap") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 50, 60, 0), out);
  r.feed(ev(T::Up, 50, 60, 700), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::LongPress});
}

TEST_CASE("gesture: four swipe directions") {
  struct Case {
    int x2, y2;
    Gesture::Type want;
  };
  const Case cases[] = {{400, 500, Gesture::SwipeLeft}, {100, 500, Gesture::SwipeRight}, {250, 800, Gesture::SwipeUp},
                        {250, 200, Gesture::SwipeDown}};
  // start at (250, 500); the end points are 150 px away along one axis
  for (const Case& c : cases) {
    GestureRecognizer r;
    std::vector<Gesture> out;
    r.feed(ev(T::Down, 250, 500, 0), out);
    r.feed(ev(T::Move, (250 + c.x2) / 2, (500 + c.y2) / 2, 100), out);
    r.feed(ev(T::Up, c.x2, c.y2, 200), out);
  }
  struct S {
    int x, y;
    Gesture::Type want;
  };
  const S s[] = {{100, 500, Gesture::SwipeLeft}, {400, 500, Gesture::SwipeRight}, {250, 350, Gesture::SwipeUp},
                 {250, 650, Gesture::SwipeDown}};
  for (const S& c : s) {
    GestureRecognizer r;
    std::vector<Gesture> out;
    r.feed(ev(T::Down, 250, 500, 0), out);
    r.feed(ev(T::Move, (250 + c.x) / 2, (500 + c.y) / 2, 100), out);
    r.feed(ev(T::Up, c.x, c.y, 200), out);
    REQUIRE(out.size() == 3);
    CHECK(out[0].type == Gesture::Down);
    CHECK(out[1].type == Gesture::Cancel);  // the pressed widget lets go as soon as the finger leaves
    CHECK(out[2].type == c.want);
    CHECK(out[2].startX == 250);
    CHECK(out[2].x == c.x);
  }
}

TEST_CASE("gesture: too short, too slow or too diagonal is no swipe") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 250, 500, 0), out);
  r.feed(ev(T::Move, 200, 500, 50), out);
  r.feed(ev(T::Up, 170, 500, 100), out);  // 80 px: needs more than 80
  CHECK(types(out) == V{Gesture::Down, Gesture::Cancel});

  out.clear();
  r.feed(ev(T::Down, 250, 500, 0), out);
  r.feed(ev(T::Move, 150, 500, 400), out);
  r.feed(ev(T::Up, 50, 500, 801), out);  // 801 ms
  CHECK(types(out) == V{Gesture::Down, Gesture::Cancel});

  out.clear();
  r.feed(ev(T::Down, 250, 500, 0), out);
  r.feed(ev(T::Move, 200, 450, 50), out);
  r.feed(ev(T::Up, 100, 400, 100), out);  // 150 x 100: x dominates -> left
  CHECK(types(out) == V{Gesture::Down, Gesture::Cancel, Gesture::SwipeLeft});
  out.clear();
  r.feed(ev(T::Down, 250, 500, 0), out);
  r.feed(ev(T::Move, 200, 450, 50), out);
  r.feed(ev(T::Up, 150, 400, 100), out);  // 100 x 100: neither dominates
  CHECK(types(out) == V{Gesture::Down, Gesture::Cancel});
}

TEST_CASE("gesture: a long press then a move does not swipe") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 250, 500, 0), out);
  r.tick(600, out);
  r.feed(ev(T::Move, 100, 500, 700), out);
  r.feed(ev(T::Up, 100, 500, 750), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::LongPress});
}

TEST_CASE("gesture: a new Down without an Up cancels the old touch") {
  GestureRecognizer r;
  std::vector<Gesture> out;
  r.feed(ev(T::Down, 10, 10, 0), out);
  r.feed(ev(T::Down, 300, 300, 50), out);
  CHECK(types(out) == V{Gesture::Down, Gesture::Cancel, Gesture::Down});
}
