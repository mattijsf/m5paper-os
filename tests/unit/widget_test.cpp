#include <doctest.h>

#include <string>
#include <vector>

#include "ui/label.h"
#include "ui/layout.h"
#include "ui/screen.h"

using namespace pos;
using namespace pos::ui;

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

// A fixed-size box that records the gestures it gets and can consume them.
class Box : public Widget {
 public:
  Box(int w, int h, bool consume = false) : w(w), h(h), consume(consume) {}
  int w, h;
  bool consume;
  std::vector<Gesture::Type> got;
  int draws = 0;
  Size measure(int) override { return {w, h}; }
  void draw(Canvas& c) override {
    ++draws;
    c.fillRect(frame.x, frame.y, frame.w, frame.h, gray(8));
  }
  bool onTouch(const Gesture& g) override {
    got.push_back(g.type);
    return consume;
  }
};

struct Fixture {
  Canvas canvas;
  FakeDisplay display;
  FakeClock clock;
  DisplayManager dm;
  Screen screen;
  static Canvas& prep(Canvas& c) {
    c.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    c.createSprite(kScreenW, kScreenH);
    c.fillScreen(kWhite);
    return c;
  }
  Fixture() : dm(display, prep(canvas), clock), screen(canvas, dm) { dm.forceClean(); }
};

Gesture g(Gesture::Type t, int x, int y) {
  Gesture k;
  k.type = t;
  k.x = k.startX = static_cast<int16_t>(x);
  k.y = k.startY = static_cast<int16_t>(y);
  return k;
}

}  // namespace

TEST_CASE("Column: fixed children, gap, stretch") {
  Fixture f;
  auto root = std::make_unique<Column>(10);
  auto* a = root->add<Box>(100, 40);
  auto* b = root->add<Box>(80, 60);
  f.screen.setRoot(std::move(root));
  CHECK(a->frame.x == 0);
  CHECK(a->frame.y == 0);
  CHECK(a->frame.w == kScreenW);  // stretch
  CHECK(a->frame.h == 40);
  CHECK(b->frame.y == 50);
  CHECK(b->frame.h == 60);
}

TEST_CASE("Column: flex children share what is left, in proportion, with no pixel lost") {
  Fixture f;
  auto root = std::make_unique<Column>(10);
  auto* top = root->add<Box>(10, 100);
  auto* f1 = root->add<Box>(10, 0);
  auto* f2 = root->add<Box>(10, 0);
  auto* bottom = root->add<Box>(10, 50);
  f1->flex = 1;
  f2->flex = 2;
  f.screen.setRoot(std::move(root));
  // 960 - 100 - 50 - 3 gaps (30) = 780 left: 1:2 -> 260 and 520
  CHECK(top->frame.y == 0);
  CHECK(f1->frame.y == 110);
  CHECK(f1->frame.h == 260);
  CHECK(f2->frame.y == 380);
  CHECK(f2->frame.h == 520);
  CHECK(bottom->frame.y == 910);
  CHECK(bottom->frame.y + bottom->frame.h == kScreenH);

  // an uneven split: 3 flex children share 100 px as 33 + 33 + 34
  Fixture g2;
  auto r2 = std::make_unique<Column>();
  auto* head = r2->add<Box>(10, 860);
  Box* k[3];
  for (auto& p : k) {
    p = r2->add<Box>(10, 0);
    p->flex = 1;
  }
  g2.screen.setRoot(std::move(r2));
  (void)head;
  CHECK(k[0]->frame.h + k[1]->frame.h + k[2]->frame.h == 100);
  CHECK(k[2]->frame.y + k[2]->frame.h == kScreenH);
}

TEST_CASE("Column: Spacer, invisible children and alignment") {
  Fixture f;
  auto root = std::make_unique<Column>(0, Align::Center);
  auto* a = root->add<Box>(100, 40);
  root->add<Spacer>(24);
  auto* hidden = root->add<Box>(100, 500);
  hidden->visible = false;
  auto* b = root->add<Box>(60, 40);
  f.screen.setRoot(std::move(root));
  CHECK(a->frame.x == (kScreenW - 100) / 2);
  CHECK(b->frame.y == 64);  // 40 + 24, the hidden box takes no space
  CHECK(b->frame.x == (kScreenW - 60) / 2);
}

TEST_CASE("Row: flex, gap and vertical centering") {
  Fixture f;
  auto root = std::make_unique<Row>(8);
  auto* a = root->add<Box>(100, 40);
  auto* mid = root->add<Box>(0, 20);
  mid->flex = 1;
  auto* c = root->add<Box>(60, 80);
  f.screen.setRoot(std::move(root));
  CHECK(a->frame.x == 0);
  CHECK(mid->frame.x == 108);
  CHECK(mid->frame.w == kScreenW - 100 - 60 - 16);
  CHECK(c->frame.x == kScreenW - 60);
  CHECK(a->frame.y == (kScreenH - 40) / 2);  // centered in the 960 px tall row
}

TEST_CASE("Padding and Stack") {
  Fixture f;
  auto root = std::make_unique<Padding>(24, 16, 24, 16);
  auto* stack = root->add<Stack>();
  auto* a = stack->add<Box>(10, 10);
  auto* b = stack->add<Box>(10, 10);
  f.screen.setRoot(std::move(root));
  CHECK(stack->frame.x == 24);
  CHECK(stack->frame.y == 16);
  CHECK(stack->frame.w == kScreenW - 48);
  CHECK(a->frame.w == stack->frame.w);
  CHECK(b->frame.h == stack->frame.h);
}

TEST_CASE("Label: wrapping, long words, ellipsis, measure") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* l = root->add<Label>("aaa bbb ccc", Type::Body);
  f.screen.setRoot(std::move(root));
  const int one = measureText(f.canvas, "aaa bbb", Type::Body);
  l->maxLines = 5;
  CHECK(l->lines(f.canvas, one + 1) == std::vector<std::string>{"aaa bbb", "ccc"});
  CHECK(l->lines(f.canvas, 2000) == std::vector<std::string>{"aaa bbb ccc"});
  l->maxLines = 1;
  auto cut = l->lines(f.canvas, one + 1);
  REQUIRE(cut.size() == 1);
  CHECK(cut[0].size() > 3);
  CHECK(cut[0].find("\xE2\x80\xA6") != std::string::npos);  // ends with an ellipsis
  CHECK(measureText(f.canvas, cut[0], Type::Body) <= one + 1);

  Label word(std::string(60, 'w'), Type::Body);
  word.maxLines = 10;
  Fixture f2;
  f2.screen.setRoot(std::make_unique<Column>());
  f2.screen.root()->adopt(std::make_unique<Label>("x"));
  auto parts = word.lines(f2.canvas, 100);
  CHECK(parts.size() > 1);
  for (auto& p : parts) CHECK(measureText(f2.canvas, p, Type::Body) <= 100);

  Label empty("", Type::Body);
  CHECK(empty.lines(f2.canvas, 100) == std::vector<std::string>{""});
}

TEST_CASE("Screen: Down goes to the deepest widget and bubbles until consumed") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* top = root->add<Box>(100, 100, false);   // does not consume
  auto* inner = top->add<Box>(100, 100, false);  // child of a Box: nested hit test
  auto* low = root->add<Box>(100, 100, true);
  f.screen.setRoot(std::move(root));
  inner->frame = top->frame;
  f.screen.onGesture(g(Gesture::Down, 50, 50));
  CHECK(inner->got == std::vector<Gesture::Type>{Gesture::Down});
  CHECK(top->got == std::vector<Gesture::Type>{Gesture::Down});  // bubbled up, nobody consumed
  CHECK(low->got.empty());
  f.screen.onGesture(g(Gesture::Tap, 50, 50));  // nobody consumed Down: no one gets the Tap
  CHECK(top->got.size() == 1);

  f.screen.onGesture(g(Gesture::Down, 50, 150));
  f.screen.onGesture(g(Gesture::Cancel, 90, 150));
  f.screen.onGesture(g(Gesture::SwipeLeft, 10, 150));
  CHECK(low->got == std::vector<Gesture::Type>{Gesture::Down, Gesture::Cancel, Gesture::SwipeLeft});
}

TEST_CASE("Screen: the widget that consumed Down gets Tap; swipes bubble to a parent") {
  Fixture f;
  struct Pager : Box {
    Pager() : Box(kScreenW, 300, false) {}
    bool onTouch(const Gesture& gs) override {
      got.push_back(gs.type);
      return gs.type == Gesture::SwipeLeft || gs.type == Gesture::SwipeRight;
    }
  };
  auto root = std::make_unique<Column>();
  struct Btn : Box {  // like a Button: takes Down/Tap/Cancel, lets swipes bubble
    Btn() : Box(200, 72, true) {}
    bool onTouch(const Gesture& gs) override {
      got.push_back(gs.type);
      return gs.type != Gesture::SwipeLeft && gs.type != Gesture::SwipeRight;
    }
  };
  auto* pager = root->add<Pager>();
  auto* button = pager->add<Btn>();
  f.screen.setRoot(std::move(root));
  button->frame = Rect{10, 10, 200, 72};
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK(button->got == std::vector<Gesture::Type>{Gesture::Down, Gesture::Tap});
  CHECK(pager->got.empty());  // the button consumed the touch

  button->got.clear();
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::Cancel, 60, 20));
  f.screen.onGesture(g(Gesture::SwipeLeft, 300, 20));
  CHECK(button->got == std::vector<Gesture::Type>{Gesture::Down, Gesture::Cancel, Gesture::SwipeLeft});
  CHECK(pager->got == std::vector<Gesture::Type>{Gesture::SwipeLeft});  // bubbled
}

TEST_CASE("Screen: only dirty widgets redraw, a dirty ancestor covers its children") {
  Fixture f;
  auto root = std::make_unique<Column>(10);
  auto* a = root->add<Box>(100, 40);
  auto* b = root->add<Box>(100, 40);
  auto* holder = root->add<Box>(100, 100);
  auto* inner = holder->add<Box>(50, 50);
  f.screen.setRoot(std::move(root));
  inner->frame = Rect{0, 100, 50, 50};
  f.screen.render(f.canvas, f.dm);  // initial: everything
  const int a0 = a->draws, b0 = b->draws, h0 = holder->draws, i0 = inner->draws;
  CHECK_FALSE(f.screen.dirty());

  b->invalidate();
  CHECK(f.screen.dirty());
  f.screen.render(f.canvas, f.dm);
  CHECK(b->draws == b0 + 1);
  CHECK(a->draws == a0);
  CHECK(holder->draws == h0);

  inner->invalidate();
  holder->invalidate();
  f.screen.render(f.canvas, f.dm);
  CHECK(holder->draws == h0 + 1);
  CHECK(inner->draws == i0 + 1);  // drawn once, by its parent
}

TEST_CASE("Screen: removing a widget forgets it") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* a = root->add<Box>(100, 40, true);
  f.screen.setRoot(std::move(root));
  f.screen.render(f.canvas, f.dm);
  a->invalidate();
  f.screen.onGesture(g(Gesture::Down, 10, 10));
  auto owned = f.screen.root()->remove(a);
  CHECK_FALSE(f.screen.dirty());
  CHECK(f.screen.pressed() == nullptr);
  f.screen.render(f.canvas, f.dm);  // nothing left to draw: must not touch the removed widget
  owned.reset();
}
