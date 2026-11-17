#include <Arduino.h>
#include <M5Unified.h>
#include <esp_sleep.h>

#include "core/log.h"
#include "platform/device/device.h"

namespace pos::device {

int Power::batteryMv() {
  // Voltage sags under load and the ADC is noisy: average eight samples.
  int sum = 0;
  for (int i = 0; i < 8; ++i) {
    sum += M5.Power.getBatteryVoltage();
    delay(1);
  }
  return sum / 8;
}

int Power::batteryPercent() {
  int p = M5.Power.getBatteryLevel();
  if (p < 0) return 0;
  return std::min(100, (p + 2) / 5 * 5);  // steps of 5 %
}

bool Power::charging() { return M5.Power.isCharging(); }

bool Power::usbConnected() { return HWCDC::isPlugged(); }  // SOF seen on the USB bus

hal::Wake Power::lightSleep(uint32_t maxMs) {
  if (maxMs == 0) return hal::Wake::Timer;
  waitForSound();  // light sleep stops the PWM clock: a chime would be cut, or hang on one note
  Serial.flush();
  M5.Display.waitDisplay();  // never sleep with a waveform half played
  const uint32_t t0 = millis();
  M5.Power.lightSleep(static_cast<uint64_t>(maxMs) * 1000ULL, true);  // timer + touch (GPIO 48)
  const uint32_t slept = millis() - t0;
  hal::Wake w = hal::Wake::Other;
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  switch (cause) {
    case ESP_SLEEP_WAKEUP_TIMER: w = hal::Wake::Timer; break;
    case ESP_SLEEP_WAKEUP_GPIO:
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1: w = hal::Wake::Touch; break;
    default: break;
  }
  POS_LOG("power", "light sleep asked %ums, woke after %ums, cause=%d (%s)", static_cast<unsigned>(maxMs),
          static_cast<unsigned>(slept), static_cast<int>(cause),
          w == hal::Wake::Timer ? "timer" : w == hal::Wake::Touch ? "touch" : "other");
  return w;
}

void Power::powerOff(uint32_t wakeAfterSec) {
  POS_LOG("power", "power off, wake after %us (0 = side button only)", static_cast<unsigned>(wakeAfterSec));
  waitForSound();  // a scheduled refresh switches off right after it read the relay
  Serial.flush();
  M5.Display.waitDisplay();  // the image stays on the panel with no power, but only if it is finished
  if (wakeAfterSec > 0) {
    M5.Power.timerSleep(static_cast<int>(wakeAfterSec));  // RTC alarm: does not return if it worked
    POS_LOG("power", "timerSleep returned: sleep was not entered");
  } else {
    M5.Power.powerOff();
  }
}

}  // namespace pos::device
