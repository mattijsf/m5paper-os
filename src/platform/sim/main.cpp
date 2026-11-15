// Desktop simulator entry point (Phase 0 spike).
//
//   program                      interactive SDL window; mouse = touch, S = screenshot, Q/Esc = quit
//   program --headless --shot out.png   render one frame to a PNG and exit (no window)
//
// The real simulator (scripted input, e-ink emulation, HTTP fixtures) is specified in docs/SIMULATOR.md.

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/demo.h"
#include "core/gfx.h"
#include "platform/sim/png.h"

using namespace pos;

namespace {

// The panel shows 16 gray levels: quantise so screenshots match the device.
std::vector<uint8_t> quantised(Canvas& c) {
  const auto* src = static_cast<const uint8_t*>(c.getBuffer());
  std::vector<uint8_t> out(static_cast<size_t>(kScreenW) * kScreenH);
  for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(((src[i] + 8) / 17) * 17);
  return out;
}

bool saveShot(Canvas& c, const std::string& path) {
  auto px = quantised(c);
  bool ok = sim::writePngGray8(path, px.data(), kScreenW, kScreenH);
  std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", path.c_str());
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  bool headless = false;
  std::string shot;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--headless")) headless = true;
    else if (!std::strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
  }

  Canvas canvas;
  canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!canvas.createSprite(kScreenW, kScreenH)) {
    std::fprintf(stderr, "canvas alloc failed\n");
    return 1;
  }
  drawDemo(canvas, "simulator");

  if (headless) return saveShot(canvas, shot.empty() ? "shot.png" : shot) ? 0 : 1;

  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Window* win = SDL_CreateWindow("PaperOS simulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, kScreenW,
                                     kScreenH, SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  SDL_RenderSetLogicalSize(ren, kScreenW, kScreenH);
  SDL_Texture* tex =
      SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, kScreenW, kScreenH);

  bool running = true, dirty = true;
  int shotCount = 0;
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN) {
        if (e.key.keysym.sym == SDLK_q || e.key.keysym.sym == SDLK_ESCAPE) running = false;
        if (e.key.keysym.sym == SDLK_s) saveShot(canvas, "shot-" + std::to_string(shotCount++) + ".png");
      }
      if (e.type == SDL_MOUSEBUTTONDOWN) {
        drawTouchMarker(canvas, e.button.x, e.button.y);
        dirty = true;
      }
    }
    if (dirty) {
      auto px = quantised(canvas);
      std::vector<uint8_t> rgb(px.size() * 3);
      for (size_t i = 0; i < px.size(); ++i) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = px[i];
      SDL_UpdateTexture(tex, nullptr, rgb.data(), kScreenW * 3);
      dirty = false;
    }
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, nullptr, nullptr);
    SDL_RenderPresent(ren);
    SDL_Delay(16);
  }
  SDL_DestroyTexture(tex);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
