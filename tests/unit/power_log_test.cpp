#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/fs.h"
#include "core/os.h"
#include "core/power_log.h"
#include "core/remote.h"

using namespace pos;

namespace {
class FDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect, Refresh) override {}
  bool busy() override { return false; }
  void wait() override {}
};
class FInput : public hal::Input {
 public:
  bool poll(hal::TouchEvent&) override { return false; }
};
class FClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return 1768480000 + ms / 1000; }
  void set(int64_t) override {}
  uint32_t ms = 1000;
};
class FNet : public hal::Net {
 public:
  bool wifiConnect(const std::string&, const std::string&, uint32_t, const std::string& = "", int = 0) override { return false; }
  void wifiOff() override {}
  bool wifiConnected() override { return false; }
  int rssi() override { return 0; }
  hal::WifiLink link() override { return {}; }
  std::vector<hal::WifiNetwork> scan() override { return {}; }
  hal::HttpResponse request(const hal::HttpRequest&) override { return {}; }
};
class FPower : public hal::Power {
 public:
  int mv = 4100;
  int batteryMv() override { return mv; }
  int batteryPercent() override { return 90; }
  bool charging() override { return false; }
  bool usbConnected() override { return usb; }
  bool usb = false;
  hal::Wake lightSleep(uint32_t) override { return hal::Wake::Timer; }
  void powerOff(uint32_t) override {}
};
struct Env {
  FDisplay display;
  FInput input;
  FClock clock;
  FNet net;
  FPower power;
  hal::Platform platform{display, input, clock, net, power, "test"};
  Os os{platform};
  std::string dir;
  Env() {
    char tmpl[] = "/tmp/pos-pl-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    os.setup();
  }
  ~Env() {
    const std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
  std::string csv() {
    std::string s;
    fs::readFile("/data/power.csv", s);
    return s;
  }
  static int count(const std::string& s, char c) {
    int n = 0;
    for (char x : s) n += x == c;
    return n;
  }
};
}  // namespace

TEST_CASE("power log: the line format") {
  CHECK(std::string(PowerLog::header()) == "unix,mv,state,wifiSeconds,pushes,usb,app\n");
  CHECK(PowerLog::formatLine(1768480000, 4012, true, 95, 31, false, "read") == "1768480000,4012,active,95,31,0,read\n");
  CHECK(PowerLog::formatLine(0, 3700, false, 0, 0, true, "") == "0,3700,idle,0,0,1,\n");
}

TEST_CASE("power log: a line at the first pass, then one every 10 minutes, with a header once") {
  Env e;
  e.os.powerLog().tick();
  std::string s = e.csv();
  CHECK(s.find("unix,mv,state") == 0);
  CHECK(Env::count(s, '\n') == 2);
  CHECK(s.find(",4100,idle,0,") != std::string::npos);
  e.clock.ms += 5 * 60 * 1000;
  e.os.powerLog().tick();
  CHECK(Env::count(e.csv(), '\n') == 2);  // not yet
  CHECK(e.os.powerLog().msToNext() > 4 * 60 * 1000);
  e.clock.ms += 5 * 60 * 1000 + 1;
  e.power.mv = 4050;
  e.os.powerLog().tick();
  s = e.csv();
  CHECK(Env::count(s, '\n') == 3);
  CHECK(s.find(",4050,") != std::string::npos);
  CHECK(e.os.powerLog().lines() == 2);
  CHECK(e.os.powerLog().lastLine().find(",4050,") != std::string::npos);
  CHECK(e.os.powerLog().msToNext() > 9 * 60 * 1000);
}

TEST_CASE("power log: off means no file and no deadline") {
  Env e;
  e.os.config().power.log = false;
  e.os.powerLog().tick();
  e.clock.ms += 30 * 60 * 1000;
  e.os.powerLog().tick();
  CHECK_FALSE(fs::exists("/data/power.csv"));
  CHECK(e.os.powerLog().msToNext() == kNoDeadline);
}

TEST_CASE("power log: the file is rotated when it gets big, and the loop wakes for the next line") {
  Env e;
  fs::writeFile("/data/power.csv", std::string(kPowerLogMaxBytes + 10, 'x'));
  e.os.powerLog().tick();
  CHECK(fs::exists("/data/power.old.csv"));
  std::string s = e.csv();
  CHECK(s.find("unix,mv,state") == 0);  // a fresh file with its header
  CHECK(e.os.msToNextDeadline() <= kPowerLogIntervalMs);
}

TEST_CASE("power log: state and usb come from input time and the power HAL") {
  Env e;
  e.power.usb = true;
  e.os.powerLog().tick();
  const std::string l = e.os.powerLog().lastLine();
  CHECK(l.find(",idle,0,") != std::string::npos);
  CHECK(l.substr(l.size() - 3) == "1,\n");  // usb 1, no app open (launcher)
}

TEST_CASE("remote: cat reads only the power log") {
  CHECK(parseRemoteCommand(">cat /data/power.csv").kind == RemoteCommand::Cat);
  CHECK(parseRemoteCommand(">cat").kind == RemoteCommand::None);
}
