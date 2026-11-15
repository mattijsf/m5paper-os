// HAL: the panel. Device: M5.Display (EPD driver). Simulator: SDL window + e-ink emulation.
// Only core/display_manager (P1-3) may call push(); see docs/DISPLAY.md.
#pragma once

#include <cstdint>
#include <vector>

#include "core/gfx.h"

namespace pos::hal {

class Display {
 public:
  virtual ~Display() = default;
  // Queue a region of `frame` for the panel using the given refresh mode. `frame` must stay alive
  // and unchanged until wait() returns or busy() is false.
  virtual void push(const Canvas& frame, Rect area, Refresh mode) = 0;
  // True while a waveform is still running.
  virtual bool busy() = 0;
  // Block until the panel is idle. Never call from the UI thread except right after a push that
  // the caller measures; see docs/DISPLAY.md.
  virtual void wait() = 0;
  // Optional: what the panel shows, 4 bits per pixel (two per byte, high nibble first, 0 black ..
  // 15 white, row-major, 540x960). Only the simulator can answer (its emulated panel); the
  // device returns false and callers fall back to the DisplayManager's shadow.
  virtual bool snapshot4(std::vector<uint8_t>& /*out*/) { return false; }
};

}  // namespace pos::hal
