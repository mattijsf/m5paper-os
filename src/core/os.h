// The OS object: owns the frame canvas and drives the platform's HAL.
// Phase 1 skeleton: draws the Phase 0 demo and marks touches. The App framework, event loop and
// DisplayManager replace this in P1-3 .. P1-5.
#pragma once

#include "core/gfx.h"
#include "hal/platform.h"

namespace pos {

class Os {
 public:
  explicit Os(hal::Platform& platform) : p_(platform) {}

  // Allocates the canvas, draws the first screen and pushes it. False if allocation failed.
  bool setup();
  // One pass of the main loop: handle pending input. Never blocks beyond a panel refresh.
  void loopOnce();

  Canvas& canvas() { return canvas_; }

 private:
  hal::Platform& p_;
  Canvas canvas_;
  bool ready_ = false;
};

}  // namespace pos
