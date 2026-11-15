// The OS object: owns the frame canvas, the DisplayManager, timers and the worker, and drives the
// platform's HAL. The Surface it shows (the Shell with the launcher and apps) is set by the platform main.
// Without a Surface it draws the font sheet (`fonts`) or a blank screen and marks touches.
#pragma once

#include <string>
#include <vector>

#include <functional>
#include <memory>

#include "core/config.h"
#include "core/display_manager.h"
#include "core/gesture.h"
#include "core/secrets.h"
#include "core/gfx.h"
#include "core/net.h"
#include "core/power_log.h"
#include "core/surface.h"
#include "core/timers.h"
#include "core/worker.h"
#include "hal/platform.h"

namespace pos {

// Whether a boot at `now` is the RTC wake that was set for `wakeAt` (unix seconds; 0 = none set).
bool isScheduledWake(int64_t now, int64_t wakeAt);

class Os {
 public:
  explicit Os(hal::Platform& platform)
      : p_(platform), dm_(platform.display, canvas_, platform.clock), timers_(platform.clock),
        net_(platform, config_, secrets_) {}

  // Allocates the canvas, draws the first screen and pushes it. False if allocation failed.
  // Without a Surface: "fonts" = the font specimen sheet, else a blank screen. Before setup().
  void setScreen(const std::string& name) { screen_ = name; }
  // Starts the worker thread: on the device set its stack size first (esp_pthread_set_cfg).
  // Shows a widget screen (or app): touches become gestures for it. Before setup().
  // After setup() it swaps at once: redraws and cleans the panel (one Quality flash).
  void setSurface(std::unique_ptr<Surface> s);
  // Replaces the surface at the start of the next loop pass (safe to call from the surface that is being replaced).
  void requestSurface(std::unique_ptr<Surface> s) { pendingSurface_ = std::move(s); hasPendingSurface_ = true; }
  // The power-off of auto-off and "Sleep now": with power.refreshHours > 0 the RTC wakes the device after that time for a scheduled
  // refresh (the wake time is remembered in /data/system.json), else it stays off until the side button. `seconds` overrides the
  // config (the remote >poweroff). Call Surface::prepareForPowerOff() first. Does not return on the device.
  // `thenOffSeconds` (a test, >poweroff N M): the refresh of that wake switches off once more for that long, then stays on.
  void powerOffForSleep(uint32_t seconds = 0, bool stayOnAfterRefresh = false, uint32_t thenOffSeconds = 0);
  // True right after a boot that the RTC alarm of powerOffForSleep() caused (not the side button): decided from the remembered
  // wake time, which is then forgotten.
  bool takeScheduledWake();
  // Whether the boot after a scheduled refresh should stay on (a test started with >poweroff) instead of switching off again.
  bool stayOnAfterRefresh() const { return stayOn_; }
  uint32_t offAgainAfterRefresh() const { return thenOff_; }
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
  // Loads /data/config.json and /sd/paperos_config (after the storage is mounted), applies the display
  // parameters and the time zone. Call before creating the Surface.
  void loadConfig();
  Config& config() { return config_; }
  NetService& net() { return net_; }
  // Plain-text status for the >status command: key names and lengths (never values), Wi-Fi, memory.
  std::string statusText();
  const Secrets& secrets() const { return secrets_; }
  // Applies config().display to the DisplayManager (Settings calls this after a change).
  void applyDisplayConfig();
  Timers& timers() { return timers_; }
  // Time of the last touch event (boot time before the first one).
  uint32_t lastInputMs() const { return lastInputMs_; }
  PowerLog& powerLog() { return powerLog_; }
  bool touching() const { return gestures_.touching(); }
  // Milliseconds until the loop next has work of its own (timers, settle / clean, long press); kNoDeadline if none.
  uint32_t msToNextDeadline();
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
  Config config_;
  Secrets secrets_;
  NetService net_;
  PowerLog powerLog_{*this};
  std::unique_ptr<Surface> surface_;
  GestureRecognizer gestures_;
  uint32_t lastInputMs_ = 0;
  Worker worker_;
  std::unique_ptr<Surface> pendingSurface_;
  bool hasPendingSurface_ = false;
  bool stayOn_ = false;
  uint32_t thenOff_ = 0;
  bool ready_ = false;
  std::string screen_;
};

}  // namespace pos
