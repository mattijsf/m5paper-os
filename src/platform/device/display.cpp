#include <M5Unified.h>

#include "platform/device/device.h"

namespace pos::device {

void Display::push(const Canvas& frame, Rect r, Refresh mode) {
  auto epd = mode == Refresh::Quality ? lgfx::epd_quality : mode == Refresh::Text ? lgfx::epd_text : lgfx::epd_fast;
  M5.Display.setEpdMode(epd);
  M5.Display.setClipRect(r.x, r.y, r.w, r.h);
  // pushSprite is not const in LovyanGFX, but does not modify the sprite.
  const_cast<Canvas&>(frame).pushSprite(&M5.Display, 0, 0);  // only pixels inside the clip are sent
  M5.Display.clearClipRect();                                // endWrite inside pushSprite queues the update
}

bool Display::busy() { return M5.Display.displayBusy(); }

void Display::wait() { M5.Display.waitDisplay(); }

}  // namespace pos::device
