// Demo screen drawn by shared code on both platforms (Inter text, Lucide icons).
// Temporary: replaced by the launcher app in Phase 2 (docs/ROADMAP.md P2-7). It lives in core
// only because Os draws it; it is the one place where core uses ui/.
#pragma once

#include <string>

#include "core/gfx.h"

namespace pos {

// Draws a mock home screen into the canvas. `platform` is shown in the footer.
void drawDemo(Canvas& c, const char* platform);

// Font and icon specimen sheet (simulator: `--app fonts`): every type token, special characters, icons.
void drawFontSheet(Canvas& c);

constexpr int kDemoTiles = 6;
Rect demoTileRect(int i);                       // launcher tile i (2 columns x 3 rows)
int demoTileAt(int x, int y);                   // tile under the point, -1 if none
const char* demoTileLabel(int i);
void drawDemoTile(Canvas& c, int i, const std::string& label);  // redraw one tile with another label

// Draws a touch marker (black dot, gray ring); returns the rectangle that changed.
Rect drawTouchMarker(Canvas& c, int x, int y);

}  // namespace pos
