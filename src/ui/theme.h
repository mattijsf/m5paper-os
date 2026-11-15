// Design tokens (docs/UI.md "Foundations"). Colours are pos::Color (uint32_t), only gray(0..15).
#pragma once

#include "core/gfx.h"

namespace pos::ui {

// Colour
constexpr Color ink = gray(0);        // text, icons, strokes, filled buttons
constexpr Color paper = gray(15);     // background
constexpr Color secondary = gray(5);  // secondary text, captions
constexpr Color disabled = gray(9);   // disabled text and icons
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

// Screen anatomy
constexpr int kStatusBarH = 56;
constexpr int kTitleBarH = 72;
constexpr int kTabBarH = 96;

}  // namespace pos::ui
