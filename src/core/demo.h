// Development helpers drawn straight into the canvas: the font/icon specimen sheet and a touch marker
// (used by the Os when no Surface is set). Not part of the product UI.
#pragma once

#include "core/gfx.h"

namespace pos {

// Font and icon specimen sheet (simulator: `--app fonts`): every type token, special characters, icons.
void drawFontSheet(Canvas& c);

// Draws a touch marker (black dot, gray ring); returns the rectangle that changed.
Rect drawTouchMarker(Canvas& c, int x, int y);

}  // namespace pos
