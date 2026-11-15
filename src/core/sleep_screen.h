// The screen left on the panel at power-off (docs/POWER.md "Sleep screen"): the book being read with its cover and progress, or the
// PaperOS mark; "Press the side button to wake". No clock: it stays for hours.
#pragma once

#include "core/app.h"
#include "core/gfx.h"

namespace pos {

// Draws the whole canvas. `info` may be null (PaperOS mark); `image` is the decoded picture of info->image (may be null).
void drawSleepScreen(Canvas& c, const SleepInfo* info, Canvas* image);

constexpr int kSleepImageW = 240, kSleepImageH = 360;

}  // namespace pos
