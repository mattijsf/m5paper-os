#include <sys/stat.h>

#include <algorithm>
#include <cstdio>

#include "platform/sim/png.h"
#include "platform/sim/sim.h"

namespace pos::sim {

namespace {

constexpr float kResidueFraction = 0.06f;  // Fast: share of the old value left behind where a pixel changed

uint8_t quant(uint8_t v) { return static_cast<uint8_t>((v + 8) / 17); }

// Fast mode on the panel is black and white by ordered dither, not by a threshold: M5GFX's Panel_EPD writes
// (value + Bayer * 16) < 248 ? black : white for epd_fast. So a gray line or a gray letter is in the Fast pass as dots (a
// gray(9) line: 6 of 16 pixels black), and the settle pass makes it the even gray. With a plain threshold at 50 % the
// simulator showed lighter grays not at all until the settle pass.
// The pattern is indexed by logical x, y here; the driver indexes it in panel coordinates (rotation not matched).
constexpr uint8_t kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
uint8_t fastLevel(uint8_t v, int x, int y) { return v + kBayer[((y & 3) << 2) | (x & 3)] * 16 < 248 ? 0 : 15; }

}  // namespace

void Display::setOutDir(const std::string& dir) {
  mkdir(dir.c_str(), 0775);
  logPath_ = dir + "/refresh.log";
  if (FILE* f = std::fopen(logPath_.c_str(), "w")) std::fclose(f);  // truncate
  log_.clear();
}

void Display::push(const Canvas& frame, Rect r, Refresh mode) {
  const auto* src = static_cast<const uint8_t*>(const_cast<Canvas&>(frame).getBuffer());
  const int x0 = std::max<int>(r.x, 0), y0 = std::max<int>(r.y, 0);
  const int x1 = std::min<int>(r.x + r.w, kScreenW), y1 = std::min<int>(r.y + r.h, kScreenH);
  uint32_t changed = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      size_t i = static_cast<size_t>(y) * kScreenW + x;
      uint8_t target = mode == Refresh::Fast ? fastLevel(src[i], x, y) : quant(src[i]);
      uint8_t old = panel_[i];
      if (target != old) ++changed;
      switch (mode) {
        case Refresh::Quality: residue_[i] = 0; break;
        case Refresh::Text: residue_[i] *= 0.5f; break;
        case Refresh::Fast:
          if (target != old) residue_[i] += kResidueFraction * (static_cast<float>(old) - target);
          break;
      }
      panel_[i] = target;
    }
  }
  char line[120];
  std::snprintf(line, sizeof line, "t=%u mode=%c rect=%d,%d,%d,%d changed=%u", static_cast<unsigned>(clock_.millis()),
                mode == Refresh::Quality ? 'Q' : mode == Refresh::Text ? 'T' : 'F', r.x, r.y, r.w, r.h, changed);
  log_.push_back(line);
  if (!logPath_.empty()) {
    if (FILE* f = std::fopen(logPath_.c_str(), "a")) {
      std::fprintf(f, "%s\n", line);
      std::fclose(f);
    }
  }
  counters_.changed += changed;
  counters_.area += static_cast<uint64_t>(std::max(0, x1 - x0)) * std::max(0, y1 - y0);
  counters_.pushes++;
  if (mode == Refresh::Quality) counters_.quality++;
  dirty_ = true;
}

Display::Counters Display::takeCounters() {
  Counters c = counters_;
  counters_ = Counters{};
  return c;
}

bool Display::snapshot4(std::vector<uint8_t>& out) {
  out.assign(kPixels / 2, 0);
  for (size_t i = 0; i < kPixels; i += 2) out[i / 2] = static_cast<uint8_t>((panel_[i] << 4) | panel_[i + 1]);
  return true;
}

std::string Display::logTail(int lines) const {
  std::string out;
  size_t from = log_.size() > static_cast<size_t>(lines) ? log_.size() - lines : 0;
  for (size_t i = from; i < log_.size(); ++i) out += log_[i] + "\n";
  return out;
}

std::vector<uint8_t> Display::rgbFrame() const {
  std::vector<uint8_t> rgb(kPixels * 3);
  for (size_t i = 0; i < kPixels; ++i) {
    float v = panel_[i];
    if (ghost_) v = std::min(15.0f, std::max(0.0f, v + residue_[i] * 8));  // exaggerated: residue is faint
    uint8_t g = static_cast<uint8_t>(v * 17 + 0.5f);
    rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = g;
  }
  return rgb;
}

bool Display::saveShot(const std::string& path) {
  std::vector<uint8_t> px(kPixels);
  for (size_t i = 0; i < kPixels; ++i) px[i] = static_cast<uint8_t>(panel_[i] * 17);
  bool ok = sim::writePngGray8(path, px.data(), kScreenW, kScreenH);
  std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", path.c_str());
  return ok;
}

bool Display::openWindow(int scale) {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return false;
  }
  win_ = SDL_CreateWindow("PaperOS simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreenW * scale,
                          kScreenH * scale, SDL_WINDOW_ALLOW_HIGHDPI);
  ren_ = SDL_CreateRenderer(win_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren_, kScreenW, kScreenH);
  tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);
  return win_ && ren_ && tex_;
}

void Display::closeWindow() {
  if (tex_) SDL_DestroyTexture(tex_);
  if (ren_) SDL_DestroyRenderer(ren_);
  if (win_) {
    SDL_DestroyWindow(win_);
    SDL_Quit();
  }
  tex_ = nullptr;
  ren_ = nullptr;
  win_ = nullptr;
}

void Display::present() {
  if (!win_) return;
  if (dirty_) {
    auto rgb = rgbFrame();
    SDL_UpdateTexture(tex_, nullptr, rgb.data(), kScreenW * 3);
    dirty_ = false;
  }
  SDL_RenderClear(ren_);
  SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
  SDL_RenderPresent(ren_);
}

}  // namespace pos::sim
