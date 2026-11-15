// The OS object: owns the frame canvas and drives the platform's HAL.
// Phase 1 skeleton: draws the Phase 0 demo and marks touches. The App framework, event loop and
// DisplayManager replace this in P1-3 .. P1-5.
#pragma once

#include <string>
#include <vector>

#include "core/display_manager.h"
#include "core/gfx.h"
#include "hal/platform.h"

namespace pos {

class Os {
 public:
  explicit Os(hal::Platform& platform) : p_(platform), dm_(platform.display, canvas_, platform.clock) {}

  // Allocates the canvas, draws the first screen and pushes it. False if allocation failed.
  // Which screen to draw: "" = the demo launcher, "fonts" = the font specimen sheet. Before setup().
  void setScreen(const std::string& name) { screen_ = name; }
  bool setup();
  // One pass of the main loop: handle pending input. Never blocks beyond a panel refresh.
  void loopOnce();

  Canvas& canvas() { return canvas_; }
  DisplayManager& displayManager() { return dm_; }
  // The current frame as 4-bit packed pixels: the emulated panel in the simulator, else the
  // DisplayManager's shadow (what was pushed to the panel).
  std::vector<uint8_t> frame4();

 private:
  hal::Platform& p_;
  Canvas canvas_;
  DisplayManager dm_;
  bool ready_ = false;
  std::string screen_;
};

}  // namespace pos
