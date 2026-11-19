#include <doctest.h>

#include <cstdlib>
#include <unistd.h>

#include "core/fs.h"
#include "core/power_policy.h"
#include "core/shell.h"
#include "core/sleep_screen.h"
#include "core/store.h"

using namespace pos;

TEST_CASE("decideSleep: sleeps only when every condition holds") {
  SleepInputs in;
  in.canLightSleep = true;
  in.msSinceInput = 1000;
  CHECK(decideSleep(in) == kMaxSleepMs);  // nothing pending: sleep up to a minute

  auto without = [&](auto change) {
    SleepInputs x = in;
    change(x);
    return decideSleep(x);
  };
  CHECK(without([](SleepInputs& x) { x.canLightSleep = false; }) == 0);   // simulator
  CHECK(without([](SleepInputs& x) { x.workerBusy = true; }) == 0);
  CHECK(without([](SleepInputs& x) { x.displayBusy = true; }) == 0);
  CHECK(without([](SleepInputs& x) { x.uiDirty = true; }) == 0);
  CHECK(without([](SleepInputs& x) { x.touching = true; }) == 0);
  CHECK(without([](SleepInputs& x) { x.wifiOn = true; }) == 0);
  CHECK(without([](SleepInputs& x) { x.msSinceInput = 299; }) == 0);      // a double tap may still come
  CHECK(without([](SleepInputs& x) { x.msSinceInput = 300; }) == kMaxSleepMs);
  // USB: stays awake by default so tools/device.py keeps working; the flag can allow sleeping
  CHECK(without([](SleepInputs& x) { x.usbConnected = true; }) == 0);
  CHECK(without([](SleepInputs& x) { x.usbConnected = true; x.stayAwakeOnUsb = false; }) == kMaxSleepMs);
  // the host reading drops out for a moment: a host seen in the last ten minutes still counts
  CHECK(without([](SleepInputs& x) { x.msSinceUsbHost = 2000; }) == 0);
  CHECK(without([](SleepInputs& x) { x.msSinceUsbHost = kUsbHostHoldMs; }) == kMaxSleepMs);
  CHECK(without([](SleepInputs& x) { x.msSinceUsbHost = 2000; x.stayAwakeOnUsb = false; }) == kMaxSleepMs);
}

TEST_CASE("decideSleep: the sleep ends at the next deadline, capped at a minute") {
  SleepInputs in;
  in.canLightSleep = true;
  in.msSinceInput = 5000;
  in.msToDeadline = 600;
  CHECK(decideSleep(in) == 600);   // a settle pass in 600 ms
  in.msToDeadline = 90000;
  CHECK(decideSleep(in) == kMaxSleepMs);
  in.msToDeadline = kSettleGuardMs;  // due within 50 ms: stay awake for it
  CHECK(decideSleep(in) == 0);
  in.msToDeadline = kSettleGuardMs + 1;
  CHECK(decideSleep(in) == kSettleGuardMs + 1);
  in.msToDeadline = 0;
  CHECK(decideSleep(in) == 0);
}

TEST_CASE("autoOffDue: after the limit, on battery only, never when the limit is 0") {
  CHECK_FALSE(autoOffDue(14 * 60 * 1000 + 59999, 15, false));
  CHECK(autoOffDue(15 * 60 * 1000, 15, false));
  CHECK_FALSE(autoOffDue(10 * 60 * 60 * 1000, 0, false));   // 0 = never
  CHECK_FALSE(autoOffDue(10 * 60 * 60 * 1000, 15, true));   // USB host connected
}

TEST_CASE("autoOffDue: not within the grace period after USB was seen (a suspended port shows no host)") {
  const uint32_t idle = 60 * 60 * 1000;
  CHECK(autoOffDue(idle, 15, false, kUsbNeverSeen));
  CHECK_FALSE(autoOffDue(idle, 15, false, 5 * 60 * 1000));            // seen 5 minutes ago
  CHECK_FALSE(autoOffDue(idle, 15, false, kUsbGraceMs - 1));
  CHECK(autoOffDue(idle, 15, false, kUsbGraceMs));                    // a day-long unplugged device does switch off
  CHECK_FALSE(autoOffDue(14 * 60 * 1000, 15, false, kUsbNeverSeen));  // the limit still applies
}

namespace {

class FakeDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect, Refresh) override { ++pushes; }
  bool busy() override { return false; }
  void wait() override { ++waits; }
  int waits = 0, pushes = 0;
};
class FakeInput : public hal::Input {
 public:
  bool poll(hal::TouchEvent& out) override {
    if (pending.empty()) return false;
    out = pending.back();
    pending.pop_back();
    return true;
  }
  std::vector<hal::TouchEvent> pending;
};
class FakeClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return unix; }
  int64_t unix = 0;
  void set(int64_t) override {}
  uint32_t ms = 1000;
};
class FakeNet : public hal::Net {
 public:
  bool wifiConnect(const std::string&, const std::string&, uint32_t, const std::string& = "", int = 0) override {
    on = true;
    return true;
  }
  hal::WifiLink link() override { return hal::WifiLink{}; }
  void wifiOff() override { on = false; }
  bool wifiConnected() override { return on; }
  int rssi() override { return 0; }
  std::vector<hal::WifiNetwork> scan() override { return {}; }
  hal::HttpResponse request(const hal::HttpRequest&) override { return {}; }
  bool on = false;
};
class FakePower : public hal::Power {
 public:
  int batteryMv() override { return 3900; }
  int batteryPercent() override { return 70; }
  bool charging() override { return chargingNow; }
  bool chargingNow = false;
  bool usbConnected() override { return usb; }
  bool canLightSleep() override { return true; }
  hal::Wake lightSleep(uint32_t ms) override {
    sleeps.push_back(ms);
    return hal::Wake::Timer;
  }
  void powerOff(uint32_t sec) override { offs.push_back(sec); }
  bool usb = false;
  std::vector<uint32_t> sleeps, offs;
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
  int waited = 0;
  PowerPolicy policy{os, [this](uint32_t) { ++waited; }};
  std::string dir;
  Env() {
    char tmpl[] = "/tmp/pos-pp-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    auto sh = std::make_unique<Shell>(os);
    shell = sh.get();
    shell->setLauncher([](Shell&) {
      struct L : Screen {
        void build(ui::Column& root) override { root.add<ui::Label>("home", ui::Type::Body); }
      };
      return std::make_unique<L>();
    });
    os.setSurface(std::move(sh));
    os.setup();
    for (int i = 0; i < 5; ++i) os.loopOnce();  // settle the first frame
  }
  ~Env() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

}  // namespace

TEST_CASE("PowerPolicy::idle: light sleep until the next deadline, short wait when work is pending") {
  Env e;
  e.os.config().power.autoOffMin = 0;  // keep auto-off out of this test
  e.clock.ms += 5000;
  // the status bar minute tick (60 s timer) is the next deadline: sleep until then, capped at a minute
  e.policy.idle(1000);
  REQUIRE(e.power.sleeps.size() == 1);
  CHECK(e.power.sleeps[0] > 50000);
  CHECK(e.power.sleeps[0] <= kMaxSleepMs);
  CHECK(e.waited == 0);

  // a timer in 700 ms shortens the sleep
  e.os.timers().after(700, [] {});
  e.policy.idle(700);
  REQUIRE(e.power.sleeps.size() == 2);
  CHECK(e.power.sleeps[1] == 700);

  // pending work: no sleep, the platform waits instead
  e.power.sleeps.clear();
  e.policy.idle(0);
  CHECK(e.power.sleeps.empty());
  CHECK(e.waited == 1);

  // a touch just happened: stay awake for 300 ms
  hal::TouchEvent ev;
  ev.type = hal::TouchEvent::Up;
  ev.x = 5;
  ev.y = 5;
  e.input.pending.push_back(ev);
  e.os.loopOnce();
  e.clock.ms += 100;
  e.policy.idle(500);
  CHECK(e.power.sleeps.empty());
  e.clock.ms += 300;
  e.policy.idle(500);
  CHECK(e.power.sleeps.size() == 1);

  // Wi-Fi up, worker busy: no sleep
  e.power.sleeps.clear();
  e.os.net().connectTo("Home", "pw", false);  // Wi-Fi is up (the policy reads the net service's state)
  e.policy.idle(500);
  e.os.net().disconnect();
  e.os.worker().start();
  e.os.worker().async([] { std::this_thread::sleep_for(std::chrono::milliseconds(50)); }, [] {});
  e.policy.idle(500);
  CHECK(e.power.sleeps.empty());
}

TEST_CASE("PowerPolicy::idle: on USB the device stays awake unless the config allows sleeping") {
  Env e;
  e.os.config().power.autoOffMin = 0;
  e.clock.ms += 5000;
  e.power.usb = true;
  e.policy.idle(1000);
  CHECK(e.power.sleeps.empty());  // default stayAwakeOnUsb = true
  e.os.config().power.stayAwakeOnUsb = false;
  e.policy.idle(1000);
  CHECK(e.power.sleeps.size() == 1);
}

TEST_CASE("PowerPolicy::idle: auto-off after the idle time, resumes only a book or a game, never on USB") {
  Env e;
  e.os.config().power.autoOffMin = 15;
  e.os.config().power.autoOffMinReader = 30;
  REQUIRE(e.shell->openApp("settings"));
  e.clock.ms += 14 * 60 * 1000 + 30 * 1000;  // 14.5 minutes without input
  e.os.loopOnce();                            // runs the overdue status timer
  e.policy.idle(1000);
  CHECK(e.power.offs.empty());
  // the sleep is cut short so the device wakes in time for auto-off (30 s left)
  REQUIRE_FALSE(e.power.sleeps.empty());
  CHECK(e.power.sleeps.back() <= 30000);

  e.clock.ms += 31 * 1000;  // past 15 minutes without input
  e.policy.idle(1000);
  REQUIRE(e.power.offs.size() == 1);
  CHECK(e.power.offs[0] == 0);          // until the side button is pressed
  CHECK(e.display.waits > 0);           // the panel was allowed to finish first
  CHECK_FALSE(e.shell->appOpen());      // the app was stopped (state saved)
  Store sys("/data/system.json");
  CHECK(sys.get("lastApp", std::string("x")) == "");  // an unused Settings: the next start shows the launcher (D38)

  // A book or a game comes back at the next start.
  {
    Env g;
    g.os.config().power.autoOffMin = 15;
    REQUIRE(g.shell->openApp("somdoku"));
    REQUIRE(AppRegistry::instance().find("somdoku")->info.stayOpen);
    g.clock.ms += 16 * 60 * 1000;
    g.policy.idle(1000);
    REQUIRE(g.power.offs.size() == 1);
    Store after("/data/system.json");
    CHECK(after.get("lastApp", std::string("")) == "somdoku");
  }
  // "Sleep now" (asked for) keeps whatever was open, as before.
  {
    Env s;
    REQUIRE(s.shell->openApp("settings"));
    s.shell->sleepNow();
    Store after("/data/system.json");
    CHECK(after.get("lastApp", std::string("")) == "settings");
  }

  // on USB: never
  Env u;
  u.os.config().power.autoOffMin = 1;
  u.power.usb = true;
  u.clock.ms += 10 * 60 * 1000;
  u.policy.idle(1000);
  CHECK(u.power.offs.empty());
}

TEST_CASE("PowerPolicy::idle: the reader has its own, longer auto-off limit") {
  Env e;
  e.os.config().power.autoOffMin = 15;
  e.os.config().power.autoOffMinReader = 30;
  REQUIRE(e.shell->openApp("read"));  // placeholder app with the reader's id
  e.clock.ms += 20 * 60 * 1000;
  e.policy.idle(1000);
  CHECK(e.power.offs.empty());  // 20 min < 30 min
  e.clock.ms += 11 * 60 * 1000;
  e.policy.idle(1000);
  CHECK(e.power.offs.size() == 1);
}

TEST_CASE("PowerPolicy::idle: USB seen once this boot keeps the device on when the host link goes quiet (suspended port)") {
  Env e;
  e.os.config().power.autoOffMin = 15;
  e.power.usb = true;
  e.policy.idle(1000);        // the host is seen
  e.power.usb = false;        // the Mac suspends the port: no host on the link
  e.clock.ms += 40 * 60 * 1000;
  e.policy.idle(1000);
  CHECK(e.power.offs.empty());
  e.clock.ms += 13u * 60 * 60 * 1000;  // but not for ever: after the grace period it switches off
  e.policy.idle(1000);
  CHECK(e.power.offs.size() == 1);

  Env b;                      // never saw USB: plain battery behaviour
  b.os.config().power.autoOffMin = 15;
  b.clock.ms += 16 * 60 * 1000;
  b.policy.idle(1000);
  CHECK(b.power.offs.size() == 1);

  Env c;                      // a charger without a data link counts too
  c.os.config().power.autoOffMin = 15;
  c.power.chargingNow = true;
  c.policy.idle(1000);
  c.power.chargingNow = false;
  c.clock.ms += 30 * 60 * 1000;
  c.policy.idle(1000);
  CHECK(c.power.offs.empty());
}

TEST_CASE("isScheduledWake: only a boot near the remembered alarm time") {
  CHECK_FALSE(isScheduledWake(1000000, 0));            // no alarm was set
  CHECK_FALSE(isScheduledWake(0, 1000000));            // the clock is not set
  CHECK(isScheduledWake(1000000, 1000000));
  CHECK(isScheduledWake(1000000 - 90, 1000000));       // the RTC alarm has minute resolution
  CHECK(isScheduledWake(1000000 + 180, 1000000));
  CHECK_FALSE(isScheduledWake(1000000 - 91, 1000000)); // the side button, earlier
  CHECK_FALSE(isScheduledWake(1000000 + 181, 1000000));
}

TEST_CASE("Os::powerOffForSleep: the refresh setting becomes the wake alarm, a boot at that time is a scheduled wake") {
  Env e;
  e.clock.unix = 1768480440;
  e.os.powerOffForSleep();  // refresh off: until the button
  REQUIRE(e.power.offs.size() == 1);
  CHECK(e.power.offs[0] == 0);
  CHECK_FALSE(e.os.takeScheduledWake());

  e.os.config().power.refreshHours = 2;
  e.os.powerOffForSleep();
  REQUIRE(e.power.offs.size() == 2);
  CHECK(e.power.offs[1] == 7200);
  e.clock.unix += 600;  // the side button after ten minutes: an ordinary boot, and the alarm is forgotten
  CHECK_FALSE(e.os.takeScheduledWake());
  e.clock.unix += 6600;
  CHECK_FALSE(e.os.takeScheduledWake());

  e.os.powerOffForSleep();
  e.clock.unix += 7200 + 20;  // the alarm (the boot takes a moment)
  CHECK(e.os.takeScheduledWake());
  CHECK_FALSE(e.os.stayOnAfterRefresh());
  CHECK_FALSE(e.os.takeScheduledWake());  // asked once

  e.os.powerOffForSleep(30, true);  // >poweroff 30: one refresh, then stay on
  CHECK(e.power.offs.back() == 30);
  e.clock.unix += 31;
  CHECK(e.os.takeScheduledWake());
  CHECK(e.os.stayOnAfterRefresh());

  e.os.powerOffForSleep(30, true, 45);  // >poweroff 30 45: the refresh switches off once more
  e.clock.unix += 30;
  CHECK(e.os.takeScheduledWake());
  CHECK(e.os.offAgainAfterRefresh() == 45);
  e.os.powerOffForSleep(45, true);
  e.clock.unix += 45;
  CHECK(e.os.takeScheduledWake());
  CHECK(e.os.offAgainAfterRefresh() == 0);
  CHECK(e.os.stayOnAfterRefresh());

  e.clock.unix = 0;  // no clock: a wake could not be told from the side button, so no alarm
  e.os.powerOffForSleep();
  CHECK(e.power.offs.back() == 0);
  CHECK_FALSE(e.os.takeScheduledWake());
}

TEST_CASE("sleepNote: unread count and the time of the refresh") {
  CHECK(sleepNote(0, "") == "");
  CHECK(sleepNote(3, "") == "3 unread");
  CHECK(sleepNote(3, "14:05") == "3 unread - updated 14:05");
  CHECK(sleepNote(0, "14:05") == "Updated 14:05");
}

TEST_CASE("Os::setup: a surface that keeps the panel (scheduled refresh) gets no first refresh") {
  struct Quiet : Surface {
    bool keeps = false;
    void onGesture(const Gesture&) override {}
    bool dirty() const override { return false; }
    void render(Canvas& c, DisplayManager&) override { c.fillScreen(kWhite); }
    bool keepsPanelAtBoot() const override { return keeps; }
  };
  for (bool keeps : {false, true}) {
    FakeDisplay display;
    FakeInput input;
    FakeClock clock;
    FakeNet net;
    FakePower power;
    hal::Platform platform{display, input, clock, net, power, "test"};
    Os os{platform};
    auto s = std::make_unique<Quiet>();
    s->keeps = keeps;
    os.setSurface(std::move(s));
    REQUIRE(os.setup());
    CHECK((display.pushes == 0) == keeps);
  }
}
