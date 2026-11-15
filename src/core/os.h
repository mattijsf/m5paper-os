// The OS object: owns the frame canvas, the DisplayManager, timers and the worker, and drives the
// platform's HAL. Phase 1 skeleton: shows the demo launcher; tapping a tile runs a 1 s fake job on
// the worker, tapping elsewhere marks the touch. The App framework (Phase 2) replaces the demo.
#pragma once

#include <string>
#include <vector>

#include <functional>

#include "core/display_manager.h"
#include "core/demo.h"
#include "core/gfx.h"
#include "core/timers.h"
#include "core/worker.h"
#include "hal/platform.h"

namespace pos {

class Os {
 public:
  explicit Os(hal::Platform& platform)
      : p_(platform), dm_(platform.display, canvas_, platform.clock), timers_(platform.clock) {}

  // Allocates the canvas, draws the first screen and pushes it. False if allocation failed.
  // Which screen to draw: "" = the demo launcher, "fonts" = the font specimen sheet. Before setup().
  void setScreen(const std::string& name) { screen_ = name; }
  // Starts the worker thread: on the device set its stack size first (esp_pthread_set_cfg).
  bool setup();
  // Steps 1-4 of the main loop: drain input, run finished worker callbacks, run due timers, draw
  // and commit (docs/ARCHITECTURE.md). Never blocks. core/loop.h adds the idle step.
  void loopOnce();
  // How long the platform may idle before something needs the loop: 0 if work is waiting.
  uint32_t idleWaitMs();
  // Called from the worker thread when a result is ready (the simulator wakes its event wait).
  void setWakeup(std::function<void()> fn) { worker_.setWakeup(std::move(fn)); }
  Timers& timers() { return timers_; }
  Worker& worker() { return worker_; }

  Canvas& canvas() { return canvas_; }
  DisplayManager& displayManager() { return dm_; }
  // The current frame as 4-bit packed pixels: the emulated panel in the simulator, else the
  // DisplayManager's shadow (what was pushed to the panel).
  std::vector<uint8_t> frame4();

 private:
  void startTileJob(int tile);

  hal::Platform& p_;
  Canvas canvas_;
  DisplayManager dm_;
  Timers timers_;
  Worker worker_;
  bool tileBusy_[kDemoTiles] = {};
  bool ready_ = false;
  std::string screen_;
};

}  // namespace pos
