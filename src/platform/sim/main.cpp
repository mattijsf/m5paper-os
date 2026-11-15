// Desktop simulator entry point.
//
//   program                      interactive SDL window; mouse = touch (drag = move), S = screenshot,
//                                G = ghost-residue overlay, L = print refresh log tail, Q/Esc = quit
//   program --script FILE [--headless]  run a UI script (docs/SIMULATOR.md); --data DIR (default sim-data)
//                                roots for /sd and /data; --time YYYY-MM-DDTHH:MM:SS fake clock start
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

#include "core/gfx.h"
#include "core/log.h"
#include "core/os.h"
#include "core/remote.h"
#include "hal/platform.h"
#include "core/fs.h"
#include "platform/sim/script.h"
#include "platform/sim/sim.h"

using namespace pos;

namespace pos::hal {
void logWrite(const char* line) { std::fputs(line, stderr); }
}  // namespace pos::hal

namespace {

class SimNet : public hal::Net {
 public:
  bool wifiConnect(const std::string&, const std::string&, uint32_t) override { return false; }
  void wifiOff() override {}
  bool wifiConnected() override { return false; }
  int rssi() override { return 0; }
  std::vector<hal::WifiNetwork> scan() override { return {}; }
  hal::HttpResponse request(const hal::HttpRequest&) override {
    hal::HttpResponse r;
    r.error = "not implemented in the simulator yet";
    return r;
  }
};

class SimPower : public hal::Power {
 public:
  int batteryMv() override { return 3900; }
  int batteryPercent() override { return 70; }
  bool charging() override { return false; }
  hal::Wake lightSleep(uint32_t maxMs) override {
    SDL_Delay(maxMs < 1000 ? maxMs : 1000);
    return hal::Wake::Timer;
  }
  void powerOff(uint32_t) override { std::exit(0); }
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

 private:
  sim::Input& in_;
};

// Reads stdin without blocking (waits up to waitMs). Returns false once stdin is closed.
bool readStdinLines(std::string& buf, Remote& remote, int waitMs) {
  struct pollfd pfd = {0, POLLIN, 0};
  if (poll(&pfd, 1, waitMs) > 0 && (pfd.revents & (POLLIN | POLLHUP))) {
    char tmp[256];
    ssize_t n = read(0, tmp, sizeof tmp);
    if (n <= 0) return false;
    buf.append(tmp, static_cast<size_t>(n));
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      remote.handleLine(buf.substr(0, nl));
      buf.erase(0, nl + 1);
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool headless = false, remoteMode = false;
  std::string shot, name = "simulator", outDir = "out", scriptPath, dataDir = "sim-data", startTime, appName;
  int scale = 1;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--headless")) headless = true;
    else if (!std::strcmp(argv[i], "--remote")) remoteMode = true;
    else if (!std::strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
    else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!std::strcmp(argv[i], "--script") && i + 1 < argc) scriptPath = argv[++i];
    else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataDir = argv[++i];
    else if (!std::strcmp(argv[i], "--time") && i + 1 < argc) startTime = argv[++i];
    else if (!std::strcmp(argv[i], "--app") && i + 1 < argc) appName = argv[++i];  // no apps yet (P2)
    else if (!std::strcmp(argv[i], "--fixtures") && i + 1 < argc) ++i;               // P5
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) outDir = argv[++i];
    else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
  }

  sim::Clock clock;
  sim::Display display(clock);
  sim::Input input(clock);
  SimNet net;
  SimPower power;
  hal::Platform platform{display, input, clock, net, power, name.c_str()};

  display.setOutDir(outDir);
  fs::setRoots(dataDir + "/sd", dataDir + "/data");
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
  StdioRemoteHost remoteHost(input);
  Remote remote(os, clock, remoteHost);
  std::string rx;

  if (!scriptPath.empty()) {
    if (!headless && !display.openWindow(scale)) return 1;
    if (!os.setup()) return 1;
    sim::ScriptRunner runner(os, display, input, clock, outDir, [&] { display.present(); });
    int rc = runner.run(scriptPath);
    if (headless || rc != 0) return rc;
    // Windowed: leave the window open after the script, as an interactive session.
    while (!input.quit) {
      input.pump();
      os.loopOnce();
      display.present();
      SDL_Delay(16);
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
    os.loopOnce();
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
    SDL_Delay(16);
  }
  display.closeWindow();
  return 0;
}
