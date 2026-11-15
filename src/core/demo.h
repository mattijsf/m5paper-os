// Demo screen drawn by shared code on both platforms (Inter text, Lucide icons).
// Temporary: replaced by the launcher app in Phase 2 (docs/ROADMAP.md P2-7). It lives in core
// only because Os draws it; it is the one place where core uses ui/.
#pragma once

#include "core/gfx.h"

namespace pos {

// Draws a mock home screen into the canvas. `platform` is shown in the footer.
void drawDemo(Canvas& c, const char* platform);

// Font and icon specimen sheet (simulator: `--app fonts`): every type token, special characters, icons.
void drawFontSheet(Canvas& c);

// Draws a touch marker (black dot, gray ring); returns the rectangle that changed.
Rect drawTouchMarker(Canvas& c, int x, int y);

}  // namespace pos
