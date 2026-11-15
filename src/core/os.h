// The OS object: owns the frame canvas, the DisplayManager, timers and the worker, and drives the
// platform's HAL. The Surface it shows (the Shell with the launcher and apps) is set by the platform main.
// Without a Surface it draws the font sheet (`fonts`) or a blank screen and marks touches.
#pragma once

#include <string>
#include <vector>

#include <functional>
#include <memory>

#include "core/display_manager.h"
#include "core/gesture.h"
#include "core/gfx.h"
#include "core/surface.h"
#include "core/timers.h"
#include "core/worker.h"
#include "hal/platform.h"

namespace pos {

class Os {
 public:
  explicit Os(hal::Platform& platform)
      : p_(platform), dm_(platform.display, canvas_, platform.clock), timers_(platform.clock) {}

  // Allocates the canvas, draws the first screen and pushes it. False if allocation failed.
  // Without a Surface: "fonts" = the font specimen sheet, else a blank screen. Before setup().
  void setScreen(const std::string& name) { screen_ = name; }
  // Starts the worker thread: on the device set its stack size first (esp_pthread_set_cfg).
  // Shows a widget screen (or app): touches become gestures for it. Before setup().
  // After setup() it swaps at once: redraws and cleans the panel (one Quality flash).
  void setSurface(std::unique_ptr<Surface> s);
  Surface* surface() { return surface_.get(); }
  bool setup();
  // Steps 1-4 of the main loop: drain input, run finished worker callbacks, run due timers, draw
  // and commit (docs/ARCHITECTURE.md). Never blocks. core/loop.h adds the idle step.
  void loopOnce();
  // How long the platform may idle before something needs the loop: 0 if work is waiting.
  uint32_t idleWaitMs();
  // Called from the worker thread when a result is ready (the simulator wakes its event wait).
  void setWakeup(std::function<void()> fn) { worker_.setWakeup(std::move(fn)); }
  hal::Platform& platform() { return p_; }
  Timers& timers() { return timers_; }
  Worker& worker() { return worker_; }

  Canvas& canvas() { return canvas_; }
  DisplayManager& displayManager() { return dm_; }
  // The current frame as 4-bit packed pixels: the emulated panel in the simulator, else the
  // DisplayManager's shadow (what was pushed to the panel).
  std::vector<uint8_t> frame4();

 private:
  hal::Platform& p_;
  Canvas canvas_;
  DisplayManager dm_;
  Timers timers_;
  std::unique_ptr<Surface> surface_;
  GestureRecognizer gestures_;
  Worker worker_;
  bool ready_ = false;
  std::string screen_;
};

}  // namespace pos
