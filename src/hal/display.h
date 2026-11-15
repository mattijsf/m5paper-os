// HAL: the panel. Device: M5.Display (EPD driver). Simulator: SDL window + e-ink emulation.
// Only core/display_manager (P1-3) may call push(); see docs/DISPLAY.md.
#pragma once

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
};

}  // namespace pos::hal
