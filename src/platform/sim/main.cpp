// Desktop simulator entry point.
//
//   program                      interactive SDL window; mouse = touch, S = screenshot, Q/Esc = quit
//   program --headless --shot out.png   render the first frame to a PNG and exit (no window)
//
// The HAL implementations here are thin (P1-1). The real simulator (scripted input, e-ink
// emulation, HTTP fixtures) is specified in docs/SIMULATOR.md.

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include "core/gfx.h"
#include "core/log.h"
#include "core/os.h"
#include "hal/platform.h"
#include "platform/sim/png.h"

using namespace pos;

namespace pos::hal {
void logWrite(const char* line) { std::fputs(line, stderr); }
}  // namespace pos::hal

namespace {

class SimDisplay : public hal::Display {
 public:
  void push(const Canvas& frame, Rect, Refresh) override {
    frame_ = &frame;
    dirty_ = true;
  }
  bool busy() override { return false; }
  void wait() override {}

  bool openWindow() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
      std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
      return false;
    }
    win_ = SDL_CreateWindow("PaperOS simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreenW, kScreenH,
                            SDL_WINDOW_ALLOW_HIGHDPI);
    ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren_, kScreenW, kScreenH);
    tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
    return true;
  }

  void closeWindow() {
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
    if (win_) SDL_Quit();
    tex_ = nullptr;
    ren_ = nullptr;
    win_ = nullptr;
  }

  void present() {
    if (!win_) return;
    if (dirty_ && frame_) {
      auto px = quantised();
      std::vector<uint8_t> rgb(px.size() * 3);
      for (size_t i = 0; i < px.size(); ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = px[i];
      SDL_UpdateTexture(tex_, nullptr, rgb.data(), kScreenW * 3);
      dirty_ = false;
    }
    SDL_RenderClear(ren_);
    SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
    SDL_RenderPresent(ren_);
  }

  bool saveShot(const std::string& path) {
    if (!frame_) return false;
    auto px = quantised();
    bool ok = sim::writePngGray8(path, px.data(), kScreenW, kScreenH);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", path.c_str());
    return ok;
  }

 private:
  // The panel shows 16 gray levels: quantise so screenshots match the device.
  std::vector<uint8_t> quantised() const {
    const auto* src = static_cast<const uint8_t*>(const_cast<Canvas*>(frame_)->getBuffer());
    std::vector<uint8_t> out(static_cast<size_t>(kScreenW) * kScreenH);
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(((src[i] + 8) / 17) * 17);
    return out;
  }

  const Canvas* frame_ = nullptr;
  bool dirty_ = false;
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* tex_ = nullptr;
};

class SimInput : public hal::Input {
 public:
  // Reads SDL events: mouse becomes touch, Q/Esc/close request quit, S requests a screenshot.
  void pump() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) quit = true;
      if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_q || e.key.keysym.sym == SDLK_ESCAPE) quit = true;
        if (e.key.keysym.sym == SDLK_s) shotRequested = true;
      }
      if (e.type == SDL_MOUSEBUTTONDOWN) push(hal::TouchEvent::Down, e.button.x, e.button.y, e.button.timestamp);
      if (e.type == SDL_MOUSEBUTTONUP) push(hal::TouchEvent::Up, e.button.x, e.button.y, e.button.timestamp);
      if (e.type == SDL_MOUSEMOTION && (e.motion.state & SDL_BUTTON_LMASK))
        push(hal::TouchEvent::Move, e.motion.x, e.motion.y, e.motion.timestamp);
    }
  }
  bool poll(hal::TouchEvent& out) override {
    if (q_.empty()) return false;
    out = q_.front();
    q_.pop_front();
    return true;
  }

  bool quit = false;
  bool shotRequested = false;

 private:
  void push(hal::TouchEvent::Type t, int x, int y, uint32_t ms) {
    hal::TouchEvent ev;
    ev.type = t;
    ev.x = static_cast<int16_t>(x);
    ev.y = static_cast<int16_t>(y);
    ev.ms = ms;
    q_.push_back(ev);
  }
  std::deque<hal::TouchEvent> q_;
};

class SimClock : public hal::Clock {
 public:
  uint32_t millis() override { return SDL_GetTicks(); }
  int64_t now() override { return static_cast<int64_t>(std::time(nullptr)); }
  void set(int64_t) override {}  // the simulator uses host time
};

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

}  // namespace

int main(int argc, char** argv) {
  bool headless = false;
  std::string shot;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--headless")) headless = true;
    else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
  }

  SimDisplay display;
  SimInput input;
  SimClock clock;
  SimNet net;
  SimPower power;
  hal::Platform platform{display, input, clock, net, power, "simulator"};

  Os os(platform);
  if (headless) {
    // SDL_GetTicks needs no SDL_Init; no window is opened.
    if (!os.setup()) return 1;
    return display.saveShot(shot.empty() ? "shot.png" : shot) ? 0 : 1;
  }

  if (!display.openWindow()) return 1;
  if (!os.setup()) return 1;

  int shotCount = 0;
  while (!input.quit) {
    input.pump();
    os.loopOnce();
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
