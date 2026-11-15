#include <doctest.h>

#include <string>
#include <vector>

#include "ui/label.h"
#include "ui/layout.h"
#include "ui/root.h"

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
  Root screen;
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

TEST_CASE("Root: Down goes to the deepest widget and bubbles until consumed") {
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

TEST_CASE("Root: the widget that consumed Down gets Tap; swipes bubble to a parent") {
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

TEST_CASE("Root: only dirty widgets redraw, a dirty ancestor covers its children") {
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

TEST_CASE("Root: removing a widget forgets it") {
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

// ---- P2-2 controls

#include "ui/controls.h"
#include "ui/list.h"
#include "ui/tile.h"

TEST_CASE("Button: pressed on Down, click on Tap, no click after Cancel or LongPress") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* b = root->add<Button>("OK");
  int clicks = 0, longs = 0;
  b->onClick = [&] { ++clicks; };
  b->onLongPress = [&] { ++longs; };
  f.screen.setRoot(std::move(root));
  f.screen.render(f.canvas, f.dm);

  f.screen.onGesture(g(Gesture::Down, 20, 20));
  CHECK(b->pressed());
  CHECK(f.screen.dirty());  // the pressed look is queued
  CHECK(clicks == 0);       // not yet: the action fires on up
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK_FALSE(b->pressed());
  CHECK(clicks == 1);

  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::Cancel, 100, 20));  // finger slid away
  CHECK_FALSE(b->pressed());
  CHECK(clicks == 1);

  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::LongPress, 20, 20));
  CHECK(longs == 1);
  CHECK(clicks == 1);

  b->enabled = false;
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK(clicks == 1);  // disabled buttons swallow the touch
}

TEST_CASE("Button: measure and a 72 px target") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* b = root->add<Button>("Cancel");
  f.screen.setRoot(std::move(root));
  CHECK(b->frame.h == kTouchMin);
  Size s = b->measure(kScreenW);
  CHECK(s.h == 72);
  CHECK(s.w >= 96);
}

TEST_CASE("Toggle: flips on Tap and reports the new state; Down alone changes nothing") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* t = root->add<Toggle>(false);
  std::vector<bool> seen;
  t->onChange = [&](bool on) { seen.push_back(on); };
  f.screen.setRoot(std::move(root));
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  CHECK_FALSE(t->on());
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK(t->on());
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK_FALSE(t->on());
  CHECK(seen == std::vector<bool>{true, false});
  t->setOn(true);  // programmatic: no callback
  CHECK(seen.size() == 2);
  CHECK(t->measure(100).h >= kTouchMin);
}

TEST_CASE("Badge text caps at 99+") {
  CHECK(badgeText(0) == "0");
  CHECK(badgeText(99) == "99");
  CHECK(badgeText(100) == "99+");
}

TEST_CASE("ListItem: row tap and the toggle inside it are separate") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* item = root->add<ListItem>("Night mode", "", true, Icon::Moon);
  int rowTaps = 0;
  bool toggled = false;
  item->onClick = [&] { ++rowTaps; };
  item->setToggle(false, [&](bool on) { toggled = on; });
  f.screen.setRoot(std::move(root));
  CHECK(item->frame.h == 96);
  REQUIRE(item->toggle() != nullptr);
  const Rect tr = item->toggle()->frame;
  CHECK(tr.x + tr.w == item->frame.x + item->frame.w);  // flush right
  CHECK(tr.h >= kTouchMin);

  f.screen.onGesture(g(Gesture::Down, tr.x + 10, tr.y + 10));
  f.screen.onGesture(g(Gesture::Tap, tr.x + 10, tr.y + 10));
  CHECK(toggled);
  CHECK(rowTaps == 0);
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK(rowTaps == 1);

  item->setChevron();  // replacing the trailing element removes the toggle
  CHECK(item->toggle() == nullptr);
}

TEST_CASE("Pager: arrows, swipes, clamping, onPage, disabled ends") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* pager = root->add<Pager>(3);
  pager->flex = 1;
  auto* content = pager->add<Box>(100, 100, false);
  std::vector<int> pages;
  pager->onPage = [&](int p) { pages.push_back(p); };
  f.screen.setRoot(std::move(root));
  CHECK(pager->bar()->frame.h == kTouchMin);
  CHECK(pager->bar()->frame.y + kTouchMin == kScreenH);
  CHECK(content->frame.h == kScreenH - kTouchMin);

  const Rect bar = pager->bar()->frame;
  const int next = bar.x + bar.w - 20, prev = bar.x + 20, by = bar.y + 10;
  f.screen.onGesture(g(Gesture::Down, prev, by));  // already on the first page: nothing to press
  f.screen.onGesture(g(Gesture::Tap, prev, by));
  CHECK(pager->page() == 0);

  f.screen.onGesture(g(Gesture::Down, next, by));
  f.screen.onGesture(g(Gesture::Tap, next, by));
  CHECK(pager->page() == 1);
  Gesture sw = g(Gesture::SwipeLeft, 100, 300);
  f.screen.onGesture(g(Gesture::Down, 300, 300));  // on the content: Box does not consume
  f.screen.onGesture(sw);
  CHECK(pager->page() == 2);
  f.screen.onGesture(g(Gesture::Down, 300, 300));
  f.screen.onGesture(g(Gesture::SwipeLeft, 100, 300));  // past the last page: clamped
  CHECK(pager->page() == 2);
  f.screen.onGesture(g(Gesture::Down, 100, 300));
  f.screen.onGesture(g(Gesture::SwipeRight, 300, 300));
  CHECK(pager->page() == 1);
  CHECK(pages == std::vector<int>{1, 2, 1});

  pager->setPages(1);  // fewer pages than the current one: clamped
  CHECK(pager->page() == 0);
}

TEST_CASE("Tile: measure, value and badge setters redraw") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* t = root->add<Tile>(Icon::Mail, "Mail");
  f.screen.setRoot(std::move(root));
  f.screen.render(f.canvas, f.dm);
  CHECK(t->frame.h == 196);
  t->setValue("3 new");
  CHECK(f.screen.dirty());
  f.screen.render(f.canvas, f.dm);
  t->setBadge(0);  // unchanged: no redraw
  CHECK_FALSE(f.screen.dirty());
}

// ---- P2-3 inputs and overlays

#include "ui/inputs.h"
#include "ui/panels.h"

namespace {

// Fixture with timers on a controllable clock (the toast needs them).
struct TFixture {
  Canvas canvas;
  FakeDisplay display;
  struct Clk : hal::Clock {
    uint32_t millis() override { return ms; }
    int64_t now() override { return 0; }
    void set(int64_t) override {}
    uint32_t ms = 1000;
  } clock;
  DisplayManager dm;
  Timers timers;
  Root screen;
  TFixture() : dm(display, Fixture::prep(canvas), clock), timers(clock), screen(canvas, dm, &timers) { dm.forceClean(); }
  uint8_t px(int x, int y) { return static_cast<const uint8_t*>(canvas.getBuffer())[y * kScreenW + x]; }
};

void tap(Root& s, int x, int y) {
  s.onGesture(g(Gesture::Down, x, y));
  s.onGesture(g(Gesture::Tap, x, y));
}

}  // namespace

TEST_CASE("Segmented: Down previews, Tap commits and reports once") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* seg = root->add<Segmented>(std::vector<std::string>{"A", "B", "C"}, 0);
  std::vector<int> seen;
  seg->onChange = [&](int i) { seen.push_back(i); };
  f.screen.setRoot(std::move(root));
  CHECK(seg->frame.h == kTouchMin);
  f.screen.onGesture(g(Gesture::Down, 300, 20));  // middle third
  CHECK(seg->selected() == 0);                    // not committed yet
  f.screen.onGesture(g(Gesture::Cancel, 300, 80));
  CHECK(seg->selected() == 0);
  CHECK(seen.empty());
  tap(f.screen, 500, 20);
  CHECK(seg->selected() == 2);
  tap(f.screen, 500, 20);  // the same option again: no callback
  CHECK(seen == std::vector<int>{2});
  seg->setSelected(7);  // clamped, no callback
  CHECK(seg->selected() == 2);
}

TEST_CASE("Stepper: steps, clamps and greys out at the limits") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* st = root->add<Stepper>(24, 15, 25, 1);
  std::vector<int> seen;
  st->onChange = [&](int v) { seen.push_back(v); };
  f.screen.setRoot(std::move(root));
  const Rect plus = st->children()[1]->frame, minus = st->children()[0]->frame;
  CHECK(plus.x + plus.w == st->frame.x + st->frame.w);
  CHECK(plus.w >= kTouchMin);
  tap(f.screen, plus.x + 10, plus.y + 10);
  CHECK(st->value() == 25);
  tap(f.screen, plus.x + 10, plus.y + 10);  // at the maximum: the + button is disabled
  CHECK(st->value() == 25);
  tap(f.screen, minus.x + 10, minus.y + 10);
  CHECK(st->value() == 24);
  CHECK(seen == std::vector<int>{25, 24});
  st->setValue(100);
  CHECK(st->value() == 25);
  st->setValue(-5);
  CHECK(st->value() == 15);
}

TEST_CASE("LevelBar: ten segments, 10..100 percent") {
  Fixture f;
  auto root = std::make_unique<Column>();
  auto* lb = root->add<LevelBar>(60);
  std::vector<int> seen;
  lb->onChange = [&](int p) { seen.push_back(p); };
  f.screen.setRoot(std::move(root));
  const int w = lb->frame.w;
  tap(f.screen, w * 45 / 100, 20);  // 5th segment
  CHECK(lb->percent() == 50);
  tap(f.screen, 2, 20);
  CHECK(lb->percent() == 10);
  tap(f.screen, w - 2, 20);
  CHECK(lb->percent() == 100);
  CHECK(seen == std::vector<int>{50, 10, 100});
  lb->setPercent(47);
  CHECK(lb->percent() == 50);
  lb->setPercent(0);
  CHECK(lb->percent() == 10);
  lb->setPercent(250);
  CHECK(lb->percent() == 100);
}

TEST_CASE("Sheet: modal, restores the screen below, closes on outside tap and swipe down") {
  TFixture f;
  auto root = std::make_unique<Column>();
  auto* btn = root->add<Button>("Below");
  int clicks = 0;
  btn->onClick = [&] { ++clicks; };
  auto* fillBox = root->add<Box>(100, 100);
  fillBox->flex = 1;
  f.screen.setRoot(std::move(root));
  f.screen.render(f.canvas, f.dm);
  const uint8_t before = f.px(200, 700);
  CHECK(before == 0x88);  // the box's gray(8)
  CHECK(f.px(200, 500) == 0x88);

  auto sheet = std::make_unique<Sheet>();
  sheet->content()->add<Label>("Title", Type::Title);
  auto* done = sheet->content()->add<Button>("Done");
  int closed = 0;
  sheet->onClose = [&] { ++closed; };
  Sheet* sp = sheet.get();
  f.screen.addOverlay(std::move(sheet));
  CHECK(f.screen.hasOverlay());
  CHECK(sp->panel().h <= kScreenH * 6 / 10);
  CHECK(sp->panel().y + sp->panel().h == kScreenH);
  f.screen.render(f.canvas, f.dm);
  CHECK(f.px(200, sp->panel().y + 40) == 0xFF);  // the panel is paper
  CHECK(f.px(200, sp->panel().y - 5) == 0x88);   // the screen above the sheet is untouched (no dimming)

  tap(f.screen, 40, 20);  // on the dimmed part: closes, and does not click the button below
  CHECK_FALSE(f.screen.hasOverlay());
  CHECK(clicks == 0);
  CHECK(closed == 1);
  f.screen.render(f.canvas, f.dm);
  CHECK(f.px(200, 700) == before);  // restored
  tap(f.screen, 40, 20);            // now the button gets it
  CHECK(clicks == 1);

  // Swipe down inside the sheet closes it too; the Done button closes it as well.
  auto s2 = std::make_unique<Sheet>();
  s2->content()->add<Button>("Done")->onClick = [] {};
  Sheet* s2p = s2.get();
  f.screen.addOverlay(std::move(s2));
  f.screen.onGesture(g(Gesture::Down, 270, s2p->panel().y + 30));
  f.screen.onGesture(g(Gesture::Cancel, 270, s2p->panel().y + 90));
  f.screen.onGesture(g(Gesture::SwipeDown, 270, s2p->panel().y + 200));
  CHECK_FALSE(f.screen.hasOverlay());
  (void)done;
}

TEST_CASE("Sheet: its Done button can close it from inside the click") {
  TFixture f;
  f.screen.setRoot(std::make_unique<Column>());
  auto sheet = std::make_unique<Sheet>();
  Sheet* sp = sheet.get();
  auto* done = sheet->content()->add<Button>("Done");
  done->onClick = [sp] { sp->close(); };
  f.screen.addOverlay(std::move(sheet));
  const Rect r = done->frame;
  tap(f.screen, r.x + 20, r.y + 20);
  CHECK_FALSE(f.screen.hasOverlay());
  f.screen.render(f.canvas, f.dm);  // the closed sheet is destroyed here, not inside its own handler
}

TEST_CASE("Toast: shows for 3 s, passes touches through, a new one replaces it") {
  TFixture f;
  auto root = std::make_unique<Column>();
  auto* btn = root->add<Button>("Under");
  int clicks = 0;
  btn->onClick = [&] { ++clicks; };
  btn->flex = 1;  // the button fills the screen so the toast sits on top of it
  f.screen.setRoot(std::move(root));
  f.screen.showToast("No Wi-Fi");
  CHECK(f.screen.hasOverlay());
  tap(f.screen, 100, kScreenH - 40);  // inside the toast, which does not take touches
  CHECK(clicks == 1);
  f.clock.ms += 2999;
  f.timers.runDue();
  CHECK(f.screen.hasOverlay());
  f.screen.showToast("Second");  // replaces the first one and restarts the 3 s
  f.clock.ms += 2999;
  f.timers.runDue();
  CHECK(f.screen.hasOverlay());
  f.clock.ms += 1;
  f.timers.runDue();
  CHECK_FALSE(f.screen.hasOverlay());
  CHECK(f.timers.size() == 0);
}

TEST_CASE("EmptyState: icon, title, hint and an optional button, centered") {
  Fixture f;
  auto root = std::make_unique<Column>();
  int retried = 0;
  auto* es = root->add<EmptyState>(Icon::WifiOff, "No connection", "Try again later.", "Retry", [&] { ++retried; });
  es->flex = 1;
  f.screen.setRoot(std::move(root));
  REQUIRE(es->button() != nullptr);
  const Rect b = es->button()->frame;
  CHECK(b.h == kTouchMin);
  CHECK(std::abs(b.x + b.w / 2 - kScreenW / 2) <= 1);  // centered
  tap(f.screen, b.x + 10, b.y + 10);
  CHECK(retried == 1);

  auto bare = std::make_unique<EmptyState>(Icon::Mail, "Nothing here");
  CHECK(bare->button() == nullptr);
}

// ---- press feedback delay (navigation speed rule)

TEST_CASE("Pressable: a quick tap never shows the pressed look; holding past 120 ms does") {
  TFixture f;
  auto root = std::make_unique<Column>();
  auto* b = root->add<Button>("Open");
  int clicks = 0;
  b->onClick = [&] { ++clicks; };
  f.screen.setRoot(std::move(root));
  f.screen.render(f.canvas, f.dm);
  f.dm.commit();

  // quick tap: Down, then Tap before the delay
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  CHECK_FALSE(b->pressed());
  CHECK_FALSE(f.screen.dirty());  // nothing queued: no Feedback push
  f.clock.ms += kPressDelayMs - 1;
  f.timers.runDue();
  CHECK_FALSE(b->pressed());
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK(clicks == 1);
  CHECK_FALSE(b->pressed());
  f.clock.ms += 1000;  // the cancelled press timer must not fire later
  f.timers.runDue();
  CHECK_FALSE(b->pressed());
  CHECK_FALSE(f.screen.dirty());

  // held: the pressed look appears after the delay and goes with the Tap
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.clock.ms += kPressDelayMs;
  f.timers.runDue();
  CHECK(b->pressed());
  CHECK(f.screen.dirty());
  f.screen.onGesture(g(Gesture::Tap, 20, 20));
  CHECK_FALSE(b->pressed());
  CHECK(clicks == 2);

  // a widget destroyed while its press timer is pending must not be touched
  f.screen.onGesture(g(Gesture::Down, 20, 20));
  f.screen.root()->remove(b);  // destroys the Button (the returned owner is dropped)
  f.clock.ms += 1000;
  f.timers.runDue();           // fires the stale timer: must be a no-op
  CHECK(clicks == 2);
}

namespace {
// Asks for a height that shrinks as the width grows (like wrapping text).
struct WrapLike : pos::ui::Widget {
  pos::ui::Size measure(int w) override { return {w, 10000 / std::max(1, w)}; }
};
struct FixedBox : pos::ui::Widget {
  FixedBox(int w, int h) : w_(w), h_(h) {}
  pos::ui::Size measure(int) override { return {w_, h_}; }
  int w_, h_;
};
}  // namespace

TEST_CASE("Row measures flex children at their own share of the width") {
  using namespace pos::ui;
  Row row(24);
  row.add<FixedBox>(120, 50);
  auto* wrap = row.add<WrapLike>();
  wrap->flex = 1;
  CHECK(row.measure(540).h == 50);   // share 396: 25 high
  CHECK(row.measure(200).h == 178);  // share 56: 178 high (measured at the full 200 it would be 50)
  row.frame = pos::Rect{0, 0, 200, 178};
  row.layout();
  CHECK(wrap->frame.w == 56);
  CHECK(wrap->frame.h == 178);
}
