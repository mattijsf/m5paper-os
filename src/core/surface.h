// What the Os shows and feeds with gestures: a widget screen (ui::Screen) or, later, an app's screen
// stack. Defined in core so core does not depend on ui; ui and apps implement it.
#pragma once

#include "core/display_manager.h"
#include "core/gesture.h"
#include "core/gfx.h"

namespace pos {

class Surface {
 public:
  virtual ~Surface() = default;
  // A recognised gesture (Down, Tap, LongPress, Swipe*, Cancel).
  virtual void onGesture(const Gesture& g) = 0;
  // Redraw what changed into `canvas` and tell `dm` which rectangles (docs/DISPLAY.md rule 5: once per loop pass).
  virtual void render(Canvas& canvas, DisplayManager& dm) = 0;
  // True while something waits to be rendered.
  virtual bool dirty() const = 0;
};

}  // namespace pos
