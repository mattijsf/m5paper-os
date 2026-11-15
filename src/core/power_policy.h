// Power policy: the idle step of the main loop (docs/POWER.md "Idle rules", "Auto-off").
//
// idle() is called when the loop has nothing to do. It light-sleeps only when everything below holds,
// otherwise it hands over to the platform's short wait. It also powers the device off after the configured
// idle time, on battery. The decisions are pure functions (decideSleep, autoOffDue) so they are unit-tested.
#pragma once

#include <cstdint>
#include <functional>

#include "core/display_manager.h"  // kNoDeadline

namespace pos {

class Os;

struct SleepInputs {
  bool canLightSleep = false;       // the platform can sleep (device yes, simulator no)
  bool workerBusy = false;          // a job runs or a result waits
  bool displayBusy = false;
  bool uiDirty = false;             // something is waiting to be drawn or pushed
  bool touching = false;            // a finger is down (gesture in progress)
  bool wifiOn = false;
  bool usbConnected = false;
  bool stayAwakeOnUsb = true;       // config power.stayAwakeOnUsb
  uint32_t msSinceInput = 0;
  uint32_t msToDeadline = kNoDeadline;  // next timer / settle / long press / auto-off, kNoDeadline if none
};

constexpr uint32_t kSleepAfterInputMs = 300;   // let double taps and swipes finish
constexpr uint32_t kSettleGuardMs = 50;        // no sleep if a settle or clean pass is due this soon
constexpr uint32_t kMaxSleepMs = 60000;        // tick at least once a minute

// 0 = do not sleep (wait briefly instead); otherwise how long to light-sleep in ms (at most kMaxSleepMs).
uint32_t decideSleep(const SleepInputs& in);

// Auto-off: true when the device was idle for `idleMs` >= the limit and nothing keeps it awake.
// limitMin 0 = never. Never while a USB host is connected (the device is being worked on).
bool autoOffDue(uint32_t idleMs, int limitMin, bool usbConnected);

class PowerPolicy {
 public:
  // `wait(ms)` is the platform's short wait (device: delay, simulator: wait for an SDL event).
  PowerPolicy(Os& os, std::function<void(uint32_t)> wait) : os_(os), wait_(std::move(wait)) {}
  // Step 5 of the loop. `maxWaitMs` = what Os::idleWaitMs() says may pass before the loop has work.
  void idle(uint32_t maxWaitMs);
  // Power off now: apps save their state, the panel finishes, then power.powerOff(0).
  void powerOffNow();
  uint32_t sleepCount() const { return sleeps_; }

 private:
  Os& os_;
  std::function<void(uint32_t)> wait_;
  uint32_t sleeps_ = 0;
};

}  // namespace pos
