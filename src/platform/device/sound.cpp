// hal::chime() on the PaperS3: the passive buzzer on GPIO 21 (docs/HARDWARE.md), driven as a square wave by the LEDC
// timer. M5Unified's speaker class would drive the same pin through I2S and a task of its own that stays; this is a few
// lines and uses no power between chimes (the pin is released again). The notes play on a short-lived task: the caller
// (the worker that read the relay, or the UI thread) goes on at once.
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "core/log.h"
#include "hal/system.h"
#include "platform/device/device.h"

namespace pos::device {

namespace {

constexpr uint8_t kBuzzerPin = 21;
struct Note {
  uint16_t hz, ms;  // hz 0: a pause
};
// Two rising notes, a quarter of a second in all. Not heard on the device yet (STATUS.md "Pending device checks"): the
// pitch may want tuning to where this buzzer is loudest.
constexpr Note kChime[] = {{2093, 70}, {0, 40}, {2794, 130}};
constexpr uint32_t kChimeMaxMs = 1000;  // nobody waits longer for it than this

std::atomic<bool> gPlaying{false};

void chimeTask(void*) {
  if (ledcAttach(kBuzzerPin, 2000, 10)) {
    for (const Note& n : kChime) {
      ledcWriteTone(kBuzzerPin, n.hz);
      vTaskDelay(pdMS_TO_TICKS(n.ms));
    }
    ledcWriteTone(kBuzzerPin, 0);
    ledcDetach(kBuzzerPin);
  } else {
    POS_LOG("sound", "the buzzer's PWM could not be set up");
  }
  pinMode(kBuzzerPin, OUTPUT);  // released by LEDC: held low, no current through the buzzer
  digitalWrite(kBuzzerPin, LOW);
  gPlaying = false;
  vTaskDelete(nullptr);
}

void startChime() {
  bool idle = false;
  if (!gPlaying.compare_exchange_strong(idle, true)) return;  // one is playing: that one is enough
  if (xTaskCreate(chimeTask, "chime", 3072, nullptr, 2, nullptr) != pdPASS) {
    gPlaying = false;
    POS_LOG("sound", "no task for the chime");
  }
}

}  // namespace

void chime() { startChime(); }

void waitForSound() {
  const uint32_t t0 = millis();
  while (gPlaying && millis() - t0 < kChimeMaxMs) delay(10);
}

}  // namespace pos::device

namespace pos::hal {

void chime() { device::chime(); }

}  // namespace pos::hal
