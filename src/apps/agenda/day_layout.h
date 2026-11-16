// The agenda's day view as geometry (docs/apps/agenda.md "Day"): which hours the grid shows and where each event's block
// goes. Pure, unit-tested; apps/agenda/day_view draws it.
#pragma once

#include <vector>

#include "core/agenda.h"

namespace pos::agenda {

// The hours the grid shows: at least 08:00 to 20:00, widened to whole hours around the timed events. An event that began
// on an earlier day starts at the window's top; one that ends on a later day runs to midnight (the window then ends there).
struct Window {
  int from = 8 * 60, to = 20 * 60;  // minutes, multiples of 60
  int span() const { return to - from; }
};
Window windowFor(const Day& day);

// One timed event placed in the grid. Columns split the width among events whose blocks overlap on screen.
struct Block {
  int event = 0;         // index into the day's events
  int top = 0, bottom = 0;  // pixels from the grid's top
  int col = 0, cols = 1;
};
// `height`: the grid's height in pixels; `minHeight`: the smallest block (a short event still gets one readable line). All-day
// events get no block (the day view shows them above the grid).
std::vector<Block> layoutBlocks(const Day& day, const Window& w, int height, int minHeight);

// Pixel row of a minute in a grid of `height` pixels (clamped to the grid).
int rowOf(int minute, const Window& w, int height);

}  // namespace pos::agenda
