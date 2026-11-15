#include <Arduino.h>
#include <M5Unified.h>

#include "core/log.h"

#include "platform/device/device.h"

namespace pos::device {

void Display::push(const Canvas& frame, Rect r, Refresh mode) {
  const uint32_t t0 = millis();
  auto epd = mode == Refresh::Quality ? lgfx::epd_quality : mode == Refresh::Text ? lgfx::epd_text : lgfx::epd_fast;
  M5.Display.setEpdMode(epd);
  M5.Display.setClipRect(r.x, r.y, r.w, r.h);
  // pushSprite is not const in LovyanGFX, but does not modify the sprite.
  const_cast<Canvas&>(frame).pushSprite(&M5.Display, 0, 0);  // only pixels inside the clip are sent
  M5.Display.clearClipRect();                                // endWrite inside pushSprite queues the update
  if (timing_) {
    M5.Display.waitDisplay();
    const uint32_t t1 = millis();
    POS_LOG("epd", "mode=%c rect=%d,%d,%d,%d push+waveform %ums done t=%u", mode == Refresh::Quality ? 'Q' : mode == Refresh::Text ? 'T' : 'F',
            r.x, r.y, r.w, r.h, static_cast<unsigned>(t1 - t0), static_cast<unsigned>(t1));
  }
}

bool Display::busy() { return M5.Display.displayBusy(); }

void Display::wait() { M5.Display.waitDisplay(); }

}  // namespace pos::device
