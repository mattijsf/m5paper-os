// Simulator HAL implementations on SDL2 and the host. Only platform/sim may include this.
#pragma once

#include <SDL.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/net.h"

namespace pos::sim {

// Real time, or a fake clock that only moves when advance() is called (headless runs: deterministic).
class Clock : public hal::Clock {
 public:
  uint32_t millis() override;
  int64_t now() override;
  // The simulator keeps host time (or the fake start time); setting it only ends --no-rtc, like a time sync on a device
  // whose RTC was never set.
  void set(int64_t) override { rtcUnset_ = false; }
  void setFake(bool fake) { fake_ = fake; }
  void setFakeStart(int64_t unixSeconds) { fakeStartUnix_ = unixSeconds; }
  bool fake() const { return fake_; }
  void advance(uint32_t ms) { fakeMs_ += ms; }
  void setRtcUnset(bool unset) { rtcUnset_ = unset; }

 private:
  bool fake_ = false;
  std::atomic<bool> rtcUnset_{false};  // read by the worker thread
  std::atomic<uint32_t> fakeMs_{0};  // read by the worker thread
  int64_t fakeStartUnix_ = 1700000000;
  std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

// E-ink emulation (docs/DISPLAY.md "Simulator emulation"): a `panel` buffer of 16-level gray
// plus a residue layer; pushes are logged to <out>/refresh.log and kept in memory.
class Display : public hal::Display {
 public:
  explicit Display(hal::Clock& clock) : clock_(clock), panel_(kPixels, 15), residue_(kPixels, 0.0f) {}

  void push(const Canvas& frame, Rect area, Refresh mode) override;
  bool busy() override { return false; }
  void wait() override {}
  bool snapshot4(std::vector<uint8_t>& out) override;

  void setOutDir(const std::string& dir);  // creates it, truncates refresh.log
  bool openWindow(int scale);
  void closeWindow();
  void present();                           // draws the panel (or residue overlay) into the window
  bool saveShot(const std::string& path);   // PNG of the panel, 16 levels
  void toggleGhostOverlay() { ghost_ = !ghost_; dirty_ = true; }
  std::string logTail(int lines = 20) const;
  // Pixels pushed since the last call (the sum of `changed` over pushes) and Quality pushes since then.
  struct Counters {
    uint64_t changed = 0;  // panel pixels whose value changed
    uint64_t area = 0;     // sum of pushed rectangle areas
    int pushes = 0, quality = 0;
  };
  Counters takeCounters();
  const Counters& counters() const { return counters_; }

 private:
  static constexpr size_t kPixels = static_cast<size_t>(kScreenW) * kScreenH;
  std::vector<uint8_t> rgbFrame() const;

  hal::Clock& clock_;
  std::vector<uint8_t> panel_;   // 0..15 as shown by the panel
  std::vector<float> residue_;   // offset in levels left behind by Fast passes
  std::vector<std::string> log_;
  std::string logPath_;
  Counters counters_;
  bool ghost_ = false, dirty_ = true;
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* tex_ = nullptr;
};

// A fake Wi-Fi world (HomeNet on two access points, Neighbour, CafeFree) and HTTP from fixture files or libcurl.
class Net : public hal::Net {
 public:
  ~Net() override;
  bool wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs, const std::string& bssid = "",
                   int channel = 0) override;
  void wifiOff() override;
  bool wifiConnected() override { return link_.connected; }
  int rssi() override { return link_.rssi; }
  hal::WifiLink link() override { return link_; }
  std::vector<hal::WifiNetwork> scan() override;
  hal::HttpResponse request(const hal::HttpRequest& req) override;
  int64_t syncTime(uint32_t timeoutMs) override;

  // "" = real network through libcurl; else answer from <dir>/<fixture name> (core/http_fixture.h).
  void setFixturesDir(const std::string& dir) { fixturesDir_ = dir; }
  // Script `wifi crowd N`: N more networks in range ("Flat 01".."Flat N", weaker than the four of the test world), and a
  // scan misses some of them as a real one does: the first hears every second one, later scans hear them all.
  void setCrowd(int n) {
    crowd_ = n;
    scans_ = 0;
  }
  // Script `wifi off` / `wifi on`: no network is reachable.
  void setWifiAvailable(bool on) {
    wifiAvailable_ = on;
    if (!on) link_ = hal::WifiLink{};
  }

 private:
  int crowd_ = 0, scans_ = 0;
  hal::WifiLink link_;
  bool wifiAvailable_ = true;
  std::string fixturesDir_;
  std::vector<std::string> missing_;
};

class Input : public hal::Input {
 public:
  explicit Input(hal::Clock& clock) : clock_(clock) {}
  // Reads SDL events: mouse becomes touch (drag = Move); Q/Esc/close quit, S screenshot, G ghost
  // overlay, L refresh log tail.
  void pump();
  void inject(const hal::TouchEvent& ev) { q_.push_back(ev); }
  bool poll(hal::TouchEvent& out) override;

  bool quit = false, shotRequested = false, ghostRequested = false, logRequested = false;

 private:
  void add(hal::TouchEvent::Type t, int x, int y);
  hal::Clock& clock_;
  std::deque<hal::TouchEvent> q_;
};

}  // namespace pos::sim
