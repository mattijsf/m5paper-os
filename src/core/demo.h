// Phase 0 spike: one static screen drawn by shared code on both platforms.
// Replaced by the real launcher in Phase 2 (docs/ROADMAP.md).
#pragma once

#include "core/gfx.h"

namespace pos {

// Draws a mock home screen into the canvas. `platform` is shown in the footer.
void drawDemo(Canvas& c, const char* platform);

// Draws a touch marker; returns the rectangle that changed.
Rect drawTouchMarker(Canvas& c, int x, int y);

}  // namespace pos
