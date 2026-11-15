// What the Os shows and feeds with gestures: a widget screen (ui::Screen) or, later, an app's screen
// stack. Defined in core so core does not depend on ui; ui and apps implement it.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/display_manager.h"
#include "core/gesture.h"
#include "core/gfx.h"

namespace pos {

struct KeyTap {
  int x = 0, y = 0;
  bool layerSwitch = false;  // shift / 123 / abc: repaints the whole keyboard, so scripts do not size-check it
};

class Surface {
 public:
  virtual ~Surface() = default;
  // A recognised gesture (Down, Tap, LongPress, Swipe*, Cancel).
  virtual void onGesture(const Gesture& g) = 0;
  // Redraw what changed into `canvas` and tell `dm` which rectangles (docs/DISPLAY.md rule 5: once per loop pass).
  virtual void render(Canvas& canvas, DisplayManager& dm) = 0;
  // True while something waits to be rendered.
  virtual bool dirty() const = 0;
  // Screen points to tap, in order, to type `text` on the on-screen keyboard that is showing (UI scripts).
  // Called before an auto power-off: save state (the open app gets onStop) and remember what was open.
  virtual void prepareForPowerOff() {}
  // True if the panel already shows what this surface wants (the sleep screen, after an RTC wake): Os::setup() then skips the
  // full Quality refresh of the first frame.
  virtual bool keepsPanelAtBoot() const { return false; }
  // Id of the open app, "" if none (the launcher).
  virtual std::string currentAppId() const { return ""; }
  // What the top screen says about itself (UI scripts: expect-state, page-through). "" if nothing.
  virtual std::string debugState() const { return ""; }
  virtual std::vector<KeyTap> keyboardTaps(const std::string& /*text*/) { return {}; }
};

}  // namespace pos
