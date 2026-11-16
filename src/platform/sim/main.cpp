// Desktop simulator entry point.
//
//   program                      interactive SDL window; mouse = touch (drag = move), S = screenshot,
//                                G = ghost-residue overlay, L = print refresh log tail, Q/Esc = quit
//   program --script FILE [--headless]  run a UI script (docs/SIMULATOR.md); --data DIR (default sim-data)
//                                roots for /sd and /data; --time YYYY-MM-DDTHH:MM:SS fake clock start
//   program --no-rtc                    now() = 0 as on a device with an unset RTC (status bar "--:--")
//   program --out DIR --scale N  where shots and refresh.log go (default out/); window scale
//   program --headless --shot out.png   render the first frame to a PNG and exit (no window)
//   program ... --name M5PaperS3       platform name shown in the footer (to compare with a device dump)
//   program [--headless] --remote       also accept the remote commands (core/remote.h) on stdin, replies on stdout
//
// HAL: display/input/clock in display.cpp, input.cpp, clock.cpp (sim.h); Net and Power are stubs
// here. Scripts and fixtures: docs/SIMULATOR.md (P1-5).

#include <SDL.h>
#include <poll.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <algorithm>
#include <string>
#include <vector>

#include "core/companion_crypto.h"
#include "core/gfx.h"
#include "core/log.h"
#include "core/os.h"
#include "core/remote.h"
#include "hal/platform.h"
#include "hal/system.h"
#include "apps/apps.h"
#include "core/fs.h"
#include "core/loop.h"
#include "platform/posix/http_server.h"
#include "platform/sim/script.h"
#include "platform/sim/sim.h"

using namespace pos;

namespace pos::hal {
void logWrite(const char* line) { std::fputs(line, stderr); }

// Fixed fake values: screenshots of Settings > Storage / Developer stay deterministic.
StorageInfo storageInfo(const char* which) {
  StorageInfo s;
  s.present = true;
  if (std::string(which) == "sd") {
    s.totalBytes = 60917ull * 1024 * 1024;
    s.freeBytes = 60913ull * 1024 * 1024;
  } else {
    s.totalBytes = 6016ull * 1024;
    s.freeBytes = 6008ull * 1024;
  }
  return s;
}
MemoryInfo memoryInfo() { return MemoryInfo{250000, 330000, 6400000, 8388608}; }
void restart() { std::exit(0); }
std::string boardName() { return "simulator"; }
std::string macAddress() { return ""; }
}  // namespace pos::hal

namespace {

class SimPower : public hal::Power {
 public:
  int batteryMv() override { return 3900; }
  int batteryPercent() override { return 70; }
  bool charging() override { return false; }
  hal::Wake lightSleep(uint32_t maxMs) override {
    SDL_Delay(maxMs < 1000 ? maxMs : 1000);
    return hal::Wake::Timer;
  }
  void powerOff(uint32_t wakeAfterSec) override {
    if (exitOnOff) std::exit(0);
    off = true;  // a UI script goes on: `timer-wake` and `expect-off` look at this
    offSeconds = wakeAfterSec;
  }
  bool exitOnOff = true;
  bool off = false;
  uint32_t offSeconds = 0;
};

// Remote control on stdin/stdout (--remote), same commands as the device (core/remote.h).
class StdioRemoteHost : public RemoteHost {
 public:
  explicit StdioRemoteHost(sim::Input& in) : in_(in) {}
  void inject(const hal::TouchEvent& ev) override { in_.inject(ev); }
  void reply(const void* data, size_t n) override {
    std::fwrite(data, 1, n, stdout);
    std::fflush(stdout);
  }
  void reboot() override { std::exit(0); }  // the simulator has no reboot: a new run is a fresh boot
  bool option(const std::string& key, const std::string& value) override {
    if (key != "screen" || !os_) return false;
    auto s = apps::createSurface(value == "launcher" ? "" : value, *os_);
    if (!s) return false;
    os_->setSurface(std::move(s));
    return true;
  }
  Os* os_ = nullptr;

 private:
  sim::Input& in_;
};

// Reads stdin without blocking (waits up to waitMs). Returns false once stdin is closed.
bool readStdinLines(std::string& buf, Remote& remote, int waitMs) {
  struct pollfd pfd = {0, POLLIN, 0};
  if (poll(&pfd, 1, waitMs) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
    char tmp[4096];
    ssize_t n = read(0, tmp, sizeof tmp);
    if (n <= 0) return false;
    buf.append(tmp, static_cast<size_t>(n));
    for (;;) {
      if (remote.wantsRaw()) {  // an upload: the buffered bytes are file content
        std::string chunk;
        chunk.swap(buf);
        remote.feedRaw(reinterpret_cast<const uint8_t*>(chunk.data()), chunk.size());
        break;
      }
      size_t nl = buf.find('\n');
      if (nl == std::string::npos) break;
      remote.handleLine(buf.substr(0, nl));
      buf.erase(0, nl + 1);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool headless = false, remoteMode = false, noRtc = false;
  std::string shot, name = "simulator", outDir = "out", scriptPath, dataDir = "sim-data", startTime, appName, fixtures = "tests/fixtures/http";
  int scale = 1;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--headless")) headless = true;
    else if (!std::strcmp(argv[i], "--remote")) remoteMode = true;
    else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
    else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!std::strcmp(argv[i], "--script") && i + 1 < argc) scriptPath = argv[++i];
    else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataDir = argv[++i];
    else if (!std::strcmp(argv[i], "--no-rtc")) noRtc = true;
    else if (!std::strcmp(argv[i], "--time") && i + 1 < argc) startTime = argv[++i];
    else if (!std::strcmp(argv[i], "--app") && i + 1 < argc) appName = argv[++i];  // "fonts" = specimen sheet; apps arrive in P2
    else if (!std::strcmp(argv[i], "--fixtures") && i + 1 < argc) fixtures = argv[++i];  // dir, or "none" = real network
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
    else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
  }

  sim::Clock clock;
  sim::Display display(clock);
  sim::Input input(clock);
  sim::Net net;
  net.setFixturesDir(fixtures == "none" ? "" : fixtures);
  SimPower power;
  platform::PosixHttpServer http(8080);  // 80 needs root on the Mac
  hal::Platform platform{display, input, clock, net, power, name.c_str(), &http};

  display.setOutDir(outDir);
  fs::setRoots(dataDir + "/sd", dataDir + "/data");
  clock.setRtcUnset(noRtc);  // like a device whose RTC was never set: now() = 0
  if (!startTime.empty()) {
    struct tm tmv = {};
    if (!strptime(startTime.c_str(), "%Y-%m-%dT%H:%M:%S", &tmv)) {
      std::fprintf(stderr, "--time wants YYYY-MM-DDTHH:MM:SS (UTC)\n");
      return 2;
    }
    clock.setFakeStart(static_cast<int64_t>(timegm(&tmv)));
  }
  // One-shot headless renders are deterministic: the clock only moves when told to.
  if (headless && !remoteMode) clock.setFake(true);  // also for --script runs

  Os os(platform);
  os.loadConfig();
  os.setScreen(appName);
  if (auto s = apps::createBootSurface(appName, os)) os.setSurface(std::move(s));
  os.setWakeup([] {  // a finished job wakes the idle wait
    SDL_Event e = {};
    e.type = SDL_USEREVENT;
    SDL_PushEvent(&e);
  });
  // Idle step: wait for an SDL event (touch, key, worker wakeup) up to the deadline (the simulator never light-sleeps).
  PowerPolicy policy(os, [](uint32_t maxWaitMs) {
    maxWaitMs = std::min<uint32_t>(maxWaitMs, 16);  // keep stdin and the window responsive
    if (maxWaitMs == 0) return;
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
      SDL_Delay(maxWaitMs);
      return;
    }
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, static_cast<int>(maxWaitMs)) == 1) SDL_PushEvent(&e);  // let pump() see it
  });
  Loop loop(os, policy);
  StdioRemoteHost remoteHost(input);
  remoteHost.os_ = &os;
  Remote remote(os, clock, remoteHost);
  std::string rx;

  if (!scriptPath.empty()) {
    // UI scripts get repeatable "random" keys and nonces (companion pairing, COMPANION.md), so their screenshots are goldens.
    companion::setTestRandom(1);
    if (!headless && !display.openWindow(scale)) return 1;
    if (!os.setup()) return 1;
    sim::ScriptRunner runner(os, display, input, clock, net, outDir, [&] { display.present(); });
    runner.setRestart([&] {
      if (os.surface()) os.surface()->prepareForPowerOff();
      power.off = false;
      os.setSurface(apps::createBootSurface(appName, os));
    });
    power.exitOnOff = false;
    runner.setTimerWake([&]() -> std::string {
      power.off = false;
      if (os.surface()) os.surface()->prepareForPowerOff();
      os.powerOffForSleep();
      if (!power.off || power.offSeconds == 0) return "no wake timer was set (Settings > Power: the refresh is off)";
      clock.advance(power.offSeconds * 1000u);  // the RTC alarm
      power.off = false;
      os.setSurface(apps::createBootSurface(appName, os));
      return "";
    });
    runner.setOffState([&] { return power.off ? static_cast<int>(power.offSeconds) : -1; });
    int rc = runner.run(scriptPath);
    if (headless || rc != 0) return rc;
    // Windowed: leave the window open after the script, as an interactive session.
    while (!input.quit) {
      input.pump();
      loop.iterate();
      display.present();
    }
    display.closeWindow();
    return 0;
  }
  if (headless && remoteMode) {
    // Headless remote session: commands on stdin, replies on stdout, until stdin closes.
    if (!os.setup()) return 1;
    bool open = true;
    while (open || remote.busy()) {
      if (open) open = readStdinLines(rx, remote, 10);
      else SDL_Delay(10);
      os.loopOnce();
      remote.tick();
    }
    return 0;
  }
  if (headless) {
    if (!os.setup()) return 1;
    return display.saveShot(shot.empty() ? "shot.png" : shot) ? 0 : 1;
  }

  if (!display.openWindow(scale)) return 1;
  if (!os.setup()) return 1;

  int shotCount = 0;
  while (!input.quit) {
    input.pump();
    if (remoteMode && !readStdinLines(rx, remote, 0)) remoteMode = false;
    loop.iterate();  // steps 1-4 + idle (waits for an SDL event, at most 16 ms)
    remote.tick();
    if (input.ghostRequested) {
      input.ghostRequested = false;
      display.toggleGhostOverlay();
    }
    if (input.logRequested) {
      input.logRequested = false;
      std::fputs(display.logTail(20).c_str(), stderr);
    }
    if (input.shotRequested) {
      input.shotRequested = false;
      display.saveShot("shot-" + std::to_string(shotCount++) + ".png");
    }
    display.present();
  }
  display.closeWindow();
  return 0;
}
