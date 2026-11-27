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
  bool keyboard = false;            // the on-screen keyboard is showing: kSleepAfterTypingMs
  bool wifiOn = false;
  bool usbConnected = false;
  bool stayAwakeOnUsb = true;       // config power.stayAwakeOnUsb
  uint32_t msSinceUsbHost = 0xFFFFFFFFu;  // since a USB host was last seen this boot (kUsbNeverSeen if never)
  uint32_t msSinceInput = 0;
  uint32_t msToDeadline = kNoDeadline;  // next timer / settle / long press / auto-off, kNoDeadline if none
};

constexpr uint32_t kSleepAfterInputMs = 300;   // let double taps and swipes finish
// With the keyboard showing: the pause between two keys is longer than kSleepAfterInputMs, so each key woke the chip from
// light sleep (typing a Wi-Fi password on battery was slow and dropped keys).
constexpr uint32_t kSleepAfterTypingMs = 10000;
constexpr uint32_t kSettleGuardMs = 50;        // no sleep if a settle or clean pass is due this soon
constexpr uint32_t kMaxSleepMs = 60000;        // tick at least once a minute
// A USB host seen this recently still counts as connected. Two reasons (device, 2026-11-15): the reading drops out for a moment
// now and then with the port open (the nap that followed cut the serial link for 50 s), and a Mac suspends a port no program has
// open, so between two tools/device.py runs the device saw no host, napped, and the next command waited up to a minute.
// Cost: an unplugged device stays out of light sleep for these ten minutes.
constexpr uint32_t kUsbHostHoldMs = 10u * 60 * 1000;

// 0 = do not sleep (wait briefly instead); otherwise how long to light-sleep in ms (at most kMaxSleepMs).
uint32_t decideSleep(const SleepInputs& in);

// A device that saw USB (a host on the data link, or the charger) this boot is being worked on: it does not switch itself off for
// this long after the last time it saw it. The host link alone is not enough: with the port closed, the Mac suspends it and the
// device sees no host, yet a power-off then needs the side button (the device switched itself off during development).
constexpr uint32_t kUsbGraceMs = 12u * 60 * 60 * 1000;
constexpr uint32_t kUsbNeverSeen = 0xFFFFFFFFu;

// Auto-off: true when the device was idle for `idleMs` >= the limit and nothing keeps it awake.
// limitMin 0 = never. Never while a USB host is connected, nor within kUsbGraceMs of the last time USB was seen
// (`msSinceUsbSeen`, kUsbNeverSeen if not this boot).
bool autoOffDue(uint32_t idleMs, int limitMin, bool usbConnected, uint32_t msSinceUsbSeen = kUsbNeverSeen);

class PowerPolicy {
 public:
  // `wait(ms)` is the platform's short wait (device: delay, simulator: wait for an SDL event).
  PowerPolicy(Os& os, std::function<void(uint32_t)> wait) : os_(os), wait_(std::move(wait)) {}
  // Step 5 of the loop. `maxWaitMs` = what Os::idleWaitMs() says may pass before the loop has work.
  void idle(uint32_t maxWaitMs);
  // Power off now: apps save their state, the panel finishes, then power.powerOff(0).
  void powerOffNow();

 private:
  Os& os_;
  std::function<void(uint32_t)> wait_;
  bool usbSeen_ = false;
  uint32_t usbSeenMs_ = 0;  // clock.millis() when USB (host or charger) was last seen
  bool hostSeen_ = false;
  uint32_t hostSeenMs_ = 0;  // clock.millis() when a USB host was last seen (kUsbHostHoldMs)
};

}  // namespace pos
