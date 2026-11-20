#include "core/power_policy.h"

#include <algorithm>

#include "core/log.h"
#include "core/os.h"
#include "core/shell.h"

namespace pos {

uint32_t decideSleep(const SleepInputs& in) {
  if (!in.canLightSleep) return 0;
  if (in.workerBusy || in.displayBusy || in.uiDirty || in.touching || in.wifiOn) return 0;
  if (in.usbConnected && in.stayAwakeOnUsb) return 0;  // light sleep breaks the USB serial link (HARDWARE.md)
  if (in.stayAwakeOnUsb && in.msSinceUsbHost < kUsbHostHoldMs) return 0;  // seen recently: a drop-out of the reading, or the port is closed between two tool runs
  if (in.msSinceInput < kSleepAfterInputMs) return 0;
  uint32_t ms = kMaxSleepMs;
  if (in.msToDeadline != kNoDeadline) {
    if (in.msToDeadline <= kSettleGuardMs) return 0;  // something is due now: stay awake for it
    ms = std::min(ms, in.msToDeadline);
  }
  return ms;
}

bool autoOffDue(uint32_t idleMs, int limitMin, bool usbConnected, uint32_t msSinceUsbSeen) {
  if (limitMin <= 0 || usbConnected) return false;
  if (msSinceUsbSeen != kUsbNeverSeen && msSinceUsbSeen < kUsbGraceMs) return false;
  return idleMs >= static_cast<uint32_t>(limitMin) * 60u * 1000u;
}

void PowerPolicy::powerOffNow() {
  POS_LOG("power", "auto-off: saving state and powering off");
  if (Surface* s = os_.surface()) s->prepareForIdleOff();
  os_.powerOffForSleep();
}

void PowerPolicy::idle(uint32_t maxWaitMs) {
  hal::Platform& p = os_.platform();
  const uint32_t now = p.clock.millis();
  const uint32_t sinceInput = now - os_.lastInputMs();
  const Config& cfg = os_.config();

  // Auto-off first: on battery, after the configured idle time (the reader gets a longer limit).
  const bool inReader = os_.surface() && os_.surface()->currentAppId() == "read";
  const int limit = inReader ? cfg.power.autoOffMinReader : cfg.power.autoOffMin;
  if (p.power.usbConnected() || p.power.charging()) {
    usbSeen_ = true;
    usbSeenMs_ = now;
  }
  // Wireless development keeps the device on:
  // switched off, nobody could reach it until the side button. The idle sleep screen still comes (the Shell), on.
  const bool keepOn = cfg.developer.wireless;
  const uint32_t sinceUsb = usbSeen_ ? now - usbSeenMs_ : kUsbNeverSeen;
  // Told before the Shell's idle timer runs (same idle time): on its way off it leaves the panel to powerOffNow.
  os_.setOffWhenIdle(!keepOn && limit > 0 && !p.power.usbConnected() && (sinceUsb == kUsbNeverSeen || sinceUsb >= kUsbGraceMs));
  if (!keepOn && autoOffDue(sinceInput, limit, p.power.usbConnected(), sinceUsb)) {
    powerOffNow();
    return;
  }

  SleepInputs in;
  in.canLightSleep = p.power.canLightSleep();
  in.workerBusy = os_.worker().pending() > 0;
  in.displayBusy = p.display.busy();
  in.uiDirty = maxWaitMs == 0;
  in.touching = os_.touching();
  in.wifiOn = os_.net().online();
  in.usbConnected = p.power.usbConnected();
  if (in.usbConnected) {
    hostSeen_ = true;
    hostSeenMs_ = now;
  }
  in.msSinceUsbHost = hostSeen_ ? now - hostSeenMs_ : kUsbNeverSeen;
  in.stayAwakeOnUsb = cfg.power.stayAwakeOnUsb;
  in.msSinceInput = sinceInput;
  // Next thing the loop must do: the earliest of its own deadlines and the auto-off time.
  uint32_t toDeadline = os_.msToNextDeadline();
  if (limit > 0 && !in.usbConnected && !keepOn) {
    const uint32_t limitMs = static_cast<uint32_t>(limit) * 60u * 1000u;
    const uint32_t left = sinceInput >= limitMs ? 0 : limitMs - sinceInput;
    toDeadline = std::min(toDeadline, left);
  }
  in.msToDeadline = toDeadline;

  const uint32_t sleepMs = decideSleep(in);
  if (sleepMs > 0) {
    p.power.lightSleep(sleepMs);  // returns on touch (the tap is then read as usual) or timer
    return;
  }
  wait_(maxWaitMs);
}

}  // namespace pos
