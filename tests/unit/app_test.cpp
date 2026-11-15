// App framework tests. The hello app below is copied unchanged from docs/APPS.md: it must compile
// and work as written.
#include <doctest.h>

#include <cstdio>
#include <cstdlib>
#include <thread>

#include <unistd.h>

#include "core/app.h"
#include "core/fs.h"
#include "core/shell.h"
#include "ui/widgets.h"

// ---- hello, verbatim from docs/APPS.md -------------------------------------------------------
namespace pos::apps::hello {

class HelloScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Hello");
    label_ = root.add<ui::Label>("Tap the button", ui::Type::Body);
    root.add<ui::Button>("Count", [this] {
      ++count_;
      label_->setText("Tapped " + std::to_string(count_) + " times");   // invalidates itself
    });
  }

 private:
  ui::Label* label_ = nullptr;
  int count_ = 0;
};

class HelloApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<HelloScreen>(); }
};

POS_REGISTER_APP(HelloApp, {
    .id = "hello",          // unique, lowercase; also the config and cache namespace
    .name = "Hello",        // label under the launcher tile
    .icon = Icon::Info,     // from src/gen/icons.h
    .order = 90,            // position in the launcher (lower first)
});

}  // namespace pos::apps::hello
// ----------------------------------------------------------------------------------------------

using namespace pos;

namespace {

class FakeDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect, Refresh) override {}
  bool busy() override { return false; }
  void wait() override {}
};
class FakeInput : public hal::Input {
 public:
  bool poll(hal::TouchEvent&) override { return false; }
};
class FakeClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return 1700000000; }
  void set(int64_t) override {}
  uint32_t ms = 1000;
};
class FakeNet : public hal::Net {
 public:
  bool wifiConnect(const std::string&, const std::string&, uint32_t) override { return false; }
  void wifiOff() override {}
  bool wifiConnected() override { return false; }
  int rssi() override { return 0; }
  std::vector<hal::WifiNetwork> scan() override { return {}; }
  hal::HttpResponse request(const hal::HttpRequest& r) override {
    hal::HttpResponse out;
    out.status = 200;
    out.body = "reply to " + r.url;
    return out;
  }
};
class FakePower : public hal::Power {
 public:
  int batteryMv() override { return 3900; }
  int batteryPercent() override { return 70; }
  bool charging() override { return false; }
  hal::Wake lightSleep(uint32_t) override { return hal::Wake::Timer; }
  void powerOff(uint32_t) override {}
};

struct Env {
  FakeDisplay display;
  FakeInput input;
  FakeClock clock;
  FakeNet net;
  FakePower power;
  hal::Platform platform{display, input, clock, net, power, "test"};
  Os os{platform};
  Shell* shell = nullptr;
  std::string dir;
  Env() {
    char tmpl[] = "/tmp/pos-app-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    auto sh = std::make_unique<Shell>(os);
    shell = sh.get();
    shell->setLauncher([](Shell& s) {
      struct L : Screen {
        void build(ui::Column& root) override { root.add<ui::Label>("launcher", ui::Type::Body); }
      };
      (void)s;
      return std::make_unique<L>();
    });
    os.setSurface(std::move(sh));
    os.setup();
  }
  ~Env() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
  void tapAt(int x, int y) {
    Gesture g;
    g.type = Gesture::Down;
    g.x = g.startX = static_cast<int16_t>(x);
    g.y = g.startY = static_cast<int16_t>(y);
    shell->onGesture(g);
    g.type = Gesture::Tap;
    shell->onGesture(g);
    shell->render(os.canvas(), os.displayManager());
  }
  void step() { os.loopOnce(); }
};

ui::Widget* findButton(ui::Widget* w, const std::string& text) {
  if (auto* b = dynamic_cast<ui::Button*>(w))
    if (b->text() == text && b->visible) return b;
  for (auto& c : w->children())
    if (auto* r = findButton(c.get(), text)) return r;
  return nullptr;
}

ui::Label* findLabel(ui::Widget* w, const std::string& prefix) {
  if (auto* l = dynamic_cast<ui::Label*>(w))
    if (l->text().rfind(prefix, 0) == 0) return l;
  for (auto& c : w->children())
    if (auto* r = findLabel(c.get(), prefix)) return r;
  return nullptr;
}

}  // namespace

TEST_CASE("registry: hello is registered with its data, sorted by order") {
  auto all = AppRegistry::instance().sorted();
  REQUIRE_FALSE(all.empty());
  const AppEntry* hello = AppRegistry::instance().find("hello");
  REQUIRE(hello != nullptr);
  CHECK(std::string(hello->info.name) == "Hello");
  CHECK(hello->info.order == 90);
  CHECK(hello->info.icon == Icon::Info);
  for (size_t i = 1; i < all.size(); ++i) CHECK(all[i - 1]->info.order <= all[i]->info.order);
  CHECK(AppRegistry::instance().find("nope") == nullptr);
}

TEST_CASE("hello opens from the launcher, counts taps, and back returns") {
  Env env;
  CHECK_FALSE(env.shell->appOpen());
  CHECK_FALSE(env.shell->titleBar().visible);  // the launcher has no title bar
  CHECK(env.shell->depth() == 1);

  CHECK(env.shell->openApp("hello"));
  CHECK(env.shell->appOpen());
  CHECK(env.shell->depth() == 2);
  CHECK(env.shell->titleBar().visible);
  CHECK(env.shell->titleBar().title() == "Hello");

  ui::Widget* root = env.shell->root().root();
  auto* count = static_cast<ui::Button*>(findButton(root, "Count"));
  REQUIRE(count != nullptr);
  auto* label = findLabel(root, "Tap the button");
  REQUIRE(label != nullptr);
  const Rect f = count->frame;
  env.tapAt(f.x + 20, f.y + 20);
  CHECK(label->text() == "Tapped 1 times");
  env.tapAt(f.x + 20, f.y + 20);
  CHECK(label->text() == "Tapped 2 times");

  // back arrow in the title bar: leaves the app (hello has one screen)
  const Rect b = env.shell->titleBar().backButton()->frame;
  env.tapAt(b.x + 20, b.y + 20);
  CHECK_FALSE(env.shell->appOpen());
  CHECK(env.shell->depth() == 1);
  CHECK_FALSE(env.shell->titleBar().visible);
}

TEST_CASE("nav: push, pop and home; onShow/onHide/onStop order") {
  static std::vector<std::string> events;
  events.clear();
  struct Second : Screen {
    void build(ui::Column& root) override { setTitle("Second"); root.add<ui::Label>("second", ui::Type::Body); }
    void onShow() override { events.push_back("second.show"); }
    void onHide() override { events.push_back("second.hide"); }
  };
  struct First : Screen {
    void build(ui::Column& root) override {
      setTitle("First");
      root.add<ui::Button>("Next", [this] { ctx().nav.push<Second>(); });
    }
    void onShow() override { events.push_back("first.show"); }
    void onHide() override { events.push_back("first.hide"); }
  };
  struct TwoApp : App {
    void onStart(AppContext& ctx) override { ctx.nav.push<First>(); }
    void onStop() override { events.push_back("app.stop"); }
  };
  AppRegistry::instance().add({"two", "Two", Icon::Info, 91}, &createApp<TwoApp>);

  Env env;
  env.shell->openApp("two");
  CHECK(events == std::vector<std::string>{"first.show"});
  auto* next = findButton(env.shell->root().root(), "Next");
  REQUIRE(next != nullptr);
  env.tapAt(next->frame.x + 20, next->frame.y + 20);
  CHECK(env.shell->depth() == 3);  // launcher + First + Second
  CHECK(env.shell->titleBar().title() == "Second");
  CHECK(events == std::vector<std::string>{"first.show", "first.hide", "second.show"});

  const Rect b = env.shell->titleBar().backButton()->frame;
  env.tapAt(b.x + 20, b.y + 20);  // back from Second: First again, same instance
  CHECK(env.shell->depth() == 2);
  CHECK(env.shell->titleBar().title() == "First");
  CHECK(events.back() == "first.show");
  env.tapAt(b.x + 20, b.y + 20);  // back from the first screen leaves the app
  CHECK_FALSE(env.shell->appOpen());
  CHECK(events.back() == "app.stop");

  // home() from a deep screen leaves at once
  events.clear();
  env.shell->openApp("two");
  next = findButton(env.shell->root().root(), "Next");
  env.tapAt(next->frame.x + 20, next->frame.y + 20);
  env.shell->closeApp();
  CHECK(env.shell->depth() == 1);
  CHECK(events.back() == "app.stop");
}

TEST_CASE("context: async result arrives on the UI thread, and is dropped when the screen is gone") {
  static int delivered;
  static std::thread::id doneThread;
  delivered = 0;
  struct S : Screen {
    void build(ui::Column& root) override {
      root.add<ui::Label>("s", ui::Type::Body);
      ctx().async([] { return 7; }, [](int v) {
        delivered += v;
        doneThread = std::this_thread::get_id();
      });
    }
  };
  struct A : App {
    void onStart(AppContext& ctx) override { ctx.nav.push<S>(); }
  };
  AppRegistry::instance().add({"asyncapp", "Async", Icon::Info, 92}, &createApp<A>);

  Env env;
  env.shell->openApp("asyncapp");
  for (int i = 0; i < 200 && delivered == 0; ++i) {
    env.step();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(delivered == 7);
  CHECK(doneThread == std::this_thread::get_id());

  // leave before the result is delivered: dropped
  delivered = 0;
  env.shell->openApp("asyncapp");
  env.shell->closeApp();
  for (int i = 0; i < 50; ++i) {
    env.step();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(delivered == 0);
}

TEST_CASE("context: http goes through hal::Net on the worker; timers die with the screen") {
  static std::string body;
  static int fired;
  body.clear();
  fired = 0;
  struct S : Screen {
    void build(ui::Column& root) override {
      root.add<ui::Label>("s", ui::Type::Body);
      hal::HttpRequest r;
      r.url = "http://x/y";
      ctx().http(r, [](hal::HttpResponse resp) { body = resp.body; });
      ctx().timer(500, [] { ++fired; });
    }
  };
  struct A : App {
    void onStart(AppContext& ctx) override { ctx.nav.push<S>(); }
  };
  AppRegistry::instance().add({"httpapp", "Http", Icon::Info, 93}, &createApp<A>);

  Env env;
  env.shell->openApp("httpapp");
  for (int i = 0; i < 200 && body.empty(); ++i) {
    env.step();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  CHECK(body == "reply to http://x/y");
  env.clock.ms += 600;
  env.step();
  CHECK(fired == 1);

  fired = 0;
  env.shell->openApp("httpapp");  // a second app instance after closing the first
  env.shell->closeApp();
  env.shell->openApp("httpapp");
  env.shell->closeApp();
  env.clock.ms += 600;
  env.step();
  CHECK(fired == 0);  // the screens were destroyed: their timers were cancelled
}

TEST_CASE("store: round trip, types, defaults, atomic file") {
  char tmpl[] = "/tmp/pos-store-XXXXXX";
  std::string dir = mkdtemp(tmpl);
  fs::setRoots(dir + "/sd", dir + "/data");
  {
    Store s("/data/apps/t.json");
    CHECK(s.get("n", 5) == 5);
    CHECK_FALSE(s.has("n"));
    s.set("n", 42);
    s.set("flag", true);
    s.set("ratio", 1.5);
    s.set("name", "paper");
    s.set("name2", std::string("os"));
    CHECK(s.dirty());
    CHECK(s.save());
    CHECK_FALSE(s.dirty());
  }
  {
    Store s("/data/apps/t.json");  // reload from disk
    CHECK(s.get("n", 0) == 42);
    CHECK(s.get("flag", false) == true);
    CHECK(s.get("ratio", 0.0) == doctest::Approx(1.5));
    CHECK(s.get("name", "x") == "paper");
    CHECK(s.get("name2", std::string("x")) == "os");
    CHECK(s.get("missing", std::string("d")) == "d");
    CHECK(s.get("n", std::string("wrong type")) == "wrong type");  // a number is not a string
    s.remove("n");
    CHECK(s.get("n", -1) == -1);
    s.save();
  }
  std::string raw;
  REQUIRE(fs::readFile("/data/apps/t.json", raw));
  CHECK(raw.find("\"flag\":true") != std::string::npos);
  CHECK(raw.find("\"n\"") == std::string::npos);

  // a corrupt file reads as empty, never crashes
  fs::writeFile("/data/apps/bad.json", "{ not json");
  Store bad("/data/apps/bad.json");
  CHECK(bad.get("x", 3) == 3);
  bad.set("x", 4);
  CHECK(bad.save());

  // cache
  Cache cache("app");
  CHECK(cache.read("a.txt").empty());
  CHECK_FALSE(cache.exists("a.txt"));
  CHECK(cache.write("a.txt", "stale data"));
  CHECK(cache.read("a.txt") == "stale data");
  CHECK(cache.exists("a.txt"));
  std::string cmd = "rm -rf '" + dir + "'";
  (void)!std::system(cmd.c_str());
}

// ---- launcher

#include "apps/launcher/launcher.h"

namespace {

struct LEnv {
  FakeDisplay display;
  FakeInput input;
  FakeClock clock;
  FakeNet net;
  FakePower power;
  hal::Platform platform{display, input, clock, net, power, "test"};
  Os os{platform};
  Shell* shell = nullptr;
  LEnv() {
    auto sh = std::make_unique<Shell>(os);
    shell = sh.get();
    apps::launcher::install(*shell);
    os.setSurface(std::move(sh));
    os.setup();
  }
  void tapAt(int x, int y) {
    Gesture g;
    g.type = Gesture::Down;
    g.x = g.startX = static_cast<int16_t>(x);
    g.y = g.startY = static_cast<int16_t>(y);
    shell->onGesture(g);
    g.type = Gesture::Tap;
    shell->onGesture(g);
    shell->render(os.canvas(), os.displayManager());
  }
};

void collectTiles(ui::Widget* w, std::vector<ui::Tile*>& out) {
  if (auto* t = dynamic_cast<ui::Tile*>(w)) out.push_back(t);
  for (auto& c : w->children()) collectTiles(c.get(), out);
}

}  // namespace

TEST_CASE("launcher: tiles come from the registry in order; a tap opens the app; paging beyond six apps") {
  LEnv env;
  std::vector<ui::Tile*> tiles;
  collectTiles(env.shell->root().root(), tiles);
  REQUIRE(tiles.size() == 6);  // a 2 x 3 grid, reused per page
  auto apps = AppRegistry::instance().sorted();
  REQUIRE(apps.size() > 6);  // the placeholders plus the test apps: more than one page
  for (int i = 0; i < 6; ++i) CHECK(tiles[i]->label() == apps[i]->info.name);
  CHECK(tiles[0]->label() == "Read");  // order 10 comes first

  // tap the Read tile: the placeholder app opens with a "Coming soon" screen
  const Rect r = tiles[0]->frame;
  env.tapAt(r.x + 30, r.y + 30);
  CHECK(env.shell->appOpen());
  CHECK(env.shell->appId() == "read");
  CHECK(env.shell->titleBar().title() == "Read");
  env.shell->closeApp();
  CHECK_FALSE(env.shell->appOpen());

  // next page shows the following apps
  ui::Widget* root = env.shell->root().root();
  std::function<ui::Pager*(ui::Widget*)> findPager = [&](ui::Widget* w) -> ui::Pager* {
    if (auto* p = dynamic_cast<ui::Pager*>(w)) return p;
    for (auto& c : w->children())
      if (auto* p = findPager(c.get())) return p;
    return nullptr;
  };
  ui::Pager* pager = findPager(root);
  REQUIRE(pager != nullptr);
  CHECK(pager->bar()->visible);
  CHECK(pager->pages() == static_cast<int>((apps.size() + 5) / 6));
  pager->next();
  CHECK(tiles[0]->label() == apps[6]->info.name);
}
