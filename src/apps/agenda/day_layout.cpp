#include "apps/agenda/day_layout.h"

#include <algorithm>

namespace pos::agenda {

namespace {

int startOf(const Event& e, const Window& w) { return e.start < 0 ? w.from : e.start; }
int endOf(const Event& e, const Window& w) {
  if (e.end < 0) return w.to;
  return std::max(e.end, startOf(e, w));
}

}  // namespace

Window windowFor(const Day& day) {
  Window w;
  for (const Event& e : day.events) {
    if (e.allDay()) continue;
    if (e.start >= 0) w.from = std::min(w.from, e.start / 60 * 60);
    if (e.end >= 0) w.to = std::max(w.to, std::min(24 * 60, (e.end + 59) / 60 * 60));
    if (e.start >= 0 && e.end < 0) w.to = 24 * 60;  // runs past midnight: the grid goes to the end of the day
  }
  if (w.to <= w.from) w.to = std::min(24 * 60, w.from + 60);
  return w;
}

int rowOf(int minute, const Window& w, int height) {
  const int m = std::clamp(minute, w.from, w.to);
  return static_cast<int>(static_cast<long>(m - w.from) * height / std::max(1, w.span()));
}

std::vector<Block> layoutBlocks(const Day& day, const Window& w, int height, int minHeight) {
  std::vector<Block> blocks;
  for (int i = 0; i < static_cast<int>(day.events.size()); ++i) {
    const Event& e = day.events[i];
    if (e.allDay()) continue;
    Block b;
    b.event = i;
    b.top = rowOf(startOf(e, w), w, height);
    b.bottom = std::max(rowOf(endOf(e, w), w, height), b.top + minHeight);
    if (b.bottom > height) {  // a short event at the very bottom: keep its block inside the grid
      b.bottom = height;
      b.top = std::max(0, std::min(b.top, height - minHeight));
    }
    blocks.push_back(b);
  }
  std::stable_sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) {
    return a.top != b.top ? a.top < b.top : a.bottom > b.bottom;  // longer first at the same top
  });
  // Clusters of blocks that overlap on screen share the width: each block takes the first free column of its cluster.
  size_t start = 0;
  while (start < blocks.size()) {
    size_t end = start;
    int clusterBottom = blocks[start].bottom;
    std::vector<int> colBottom;  // per column: the bottom of its last block
    for (; end < blocks.size() && (end == start || blocks[end].top < clusterBottom); ++end) {
      Block& b = blocks[end];
      int col = 0;
      while (col < static_cast<int>(colBottom.size()) && colBottom[col] > b.top) ++col;
      if (col == static_cast<int>(colBottom.size())) colBottom.push_back(0);
      colBottom[col] = b.bottom;
      b.col = col;
      clusterBottom = std::max(clusterBottom, b.bottom);
    }
    for (size_t i = start; i < end; ++i) blocks[i].cols = static_cast<int>(colBottom.size());
    start = end;
  }
  return blocks;
}

}  // namespace pos::agenda
