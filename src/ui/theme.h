// Design tokens (docs/UI.md "Foundations"). Colours are pos::Color (uint32_t), only gray(0..15).
#pragma once

#include "core/gfx.h"

namespace pos::ui {

// Colour
constexpr Color ink = gray(0);        // text, icons, strokes, filled buttons
constexpr Color paper = gray(15);     // background
// The panel's white is gray and its mid-grays print lighter than on a monitor (docs/DISPLAY.md): no text lighter than gray(3), and what
// the user came to read is never gray at all (docs/UI.md "Colour").
constexpr Color secondary = gray(3);  // metadata only: times, "Updated 20:21", counts, captions, hints
constexpr Color disabled = gray(7);   // disabled text and icons
constexpr Color divider = gray(11);   // 1 px separators
constexpr Color fill = gray(13);      // icon wells, quiet backgrounds

// Spacing (base unit 8 px)
constexpr int s1 = 8, s2 = 16, s3 = 24, s4 = 32, s6 = 48;
constexpr int kMargin = 24;           // screen margin left and right
constexpr int kContentW = kScreenW - 2 * kMargin;  // 492
constexpr int kTouchMin = 72;         // minimum touch target
constexpr int kRowH = 96;             // preferred row height
constexpr int kRadius = 16;           // tiles and buttons
constexpr int kRadiusSmall = 8;       // chips
constexpr int kStroke = 2;
// A pressed look is drawn only if the finger is still down after this long: a quick tap goes straight to its
// result (one Fast pass instead of two, and no waveform still running when the next screen is built).
constexpr uint32_t kPressDelayMs = 120;

// Screen anatomy
constexpr int kStatusBarH = 56;
constexpr int kTitleBarH = 72;
constexpr int kTabBarH = 96;

}  // namespace pos::ui
