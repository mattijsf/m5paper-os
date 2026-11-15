#include "ui/layout.h"

#include <algorithm>

namespace pos::ui {

namespace {

Rect rect(int x, int y, int w, int h) {
  return Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(std::max(0, w)),
              static_cast<int16_t>(std::max(0, h))};
}

// Offset and size on the cross axis.
void crossPlace(Align a, int avail, int want, int& off, int& size) {
  switch (a) {
    case Align::Stretch: off = 0; size = avail; break;
    case Align::Start: off = 0; size = std::min(want, avail); break;
    case Align::Center: size = std::min(want, avail); off = (avail - size) / 2; break;
    case Align::End: size = std::min(want, avail); off = avail - size; break;
  }
}

}  // namespace

Size Column::measure(int availW) {
  int h = 0, n = 0, w = 0;
  for (auto& c : children()) {
    if (!c->visible) continue;
    Size s = c->measure(availW);
    h += s.h;
    w = std::max(w, s.w);
    ++n;
  }
  if (n > 1) h += gap * (n - 1);
  return {align == Align::Stretch ? availW : w, h};
}

void Column::layout() {
  std::vector<Widget*> kids;
  for (auto& c : children())
    if (c->visible) kids.push_back(c.get());
  int fixed = 0, flexSum = 0;
  std::vector<Size> sizes(kids.size());
  for (size_t i = 0; i < kids.size(); ++i) {
    sizes[i] = kids[i]->measure(frame.w);
    if (kids[i]->flex > 0) flexSum += kids[i]->flex;
    else fixed += sizes[i].h;
  }
  if (!kids.empty()) fixed += gap * static_cast<int>(kids.size() - 1);
  int left = std::max(0, frame.h - fixed), given = 0, flexSeen = 0, y = frame.y;
  for (size_t i = 0; i < kids.size(); ++i) {
    int h = sizes[i].h;
    if (kids[i]->flex > 0) {
      flexSeen += kids[i]->flex;
      int upto = left * flexSeen / flexSum;  // cumulative: the pixels left over by rounding go to the last
      h = upto - given;
      given = upto;
    }
    int off, w;
    crossPlace(align, frame.w, sizes[i].w, off, w);
    kids[i]->frame = rect(frame.x + off, y, w, h);
    kids[i]->layout();
    y += h + gap;
  }
}

// Children without flex are measured at the full width; the rest of the width is shared among the flex children by their
// weights and each of those is measured at its own share (a label that wraps in a narrow column is taller than at full width).
namespace {
struct RowSizes {
  std::vector<Size> sizes;
  int fixed = 0, flexSum = 0;
};
RowSizes measureRow(const std::vector<Widget*>& kids, int width, int gap) {
  RowSizes r;
  r.sizes.assign(kids.size(), Size{});
  for (size_t i = 0; i < kids.size(); ++i) {
    if (kids[i]->flex > 0) {
      r.flexSum += kids[i]->flex;
    } else {
      r.sizes[i] = kids[i]->measure(width);
      r.fixed += r.sizes[i].w;
    }
  }
  if (!kids.empty()) r.fixed += gap * static_cast<int>(kids.size() - 1);
  const int left = std::max(0, width - r.fixed);
  int given = 0, flexSeen = 0;
  for (size_t i = 0; i < kids.size(); ++i) {
    if (kids[i]->flex <= 0) continue;
    flexSeen += kids[i]->flex;
    const int upto = left * flexSeen / r.flexSum;
    const int share = upto - given;
    given = upto;
    r.sizes[i] = kids[i]->measure(share);
    r.sizes[i].w = share;
  }
  return r;
}
}  // namespace

Size Row::measure(int availW) {
  std::vector<Widget*> kids;
  for (auto& c : children())
    if (c->visible) kids.push_back(c.get());
  const RowSizes r = measureRow(kids, availW, gap);
  int w = 0, h = 0;
  for (const Size& s : r.sizes) {
    w += s.w;
    h = std::max(h, s.h);
  }
  if (kids.size() > 1) w += gap * static_cast<int>(kids.size() - 1);
  return {w, h};
}

void Row::layout() {
  std::vector<Widget*> kids;
  for (auto& c : children())
    if (c->visible) kids.push_back(c.get());
  const RowSizes r = measureRow(kids, frame.w, gap);
  int x = frame.x;
  for (size_t i = 0; i < kids.size(); ++i) {
    const int w = r.sizes[i].w;
    int off, h;
    crossPlace(align, frame.h, r.sizes[i].h, off, h);
    kids[i]->frame = rect(x, frame.y + off, w, h);
    kids[i]->layout();
    x += w + gap;
  }
}

Size Stack::measure(int availW) {
  Size m;
  for (auto& c : children()) {
    if (!c->visible) continue;
    Size s = c->measure(availW);
    m.w = std::max(m.w, s.w);
    m.h = std::max(m.h, s.h);
  }
  return m;
}

void Stack::layout() {
  for (auto& c : children()) {
    c->frame = frame;
    c->layout();
  }
}

Size Padding::measure(int availW) {
  Size s{};
  for (auto& c : children())
    if (c->visible) s = c->measure(availW - l - r);
  return {s.w + l + r, s.h + t + b};
}

void Padding::layout() {
  for (auto& c : children()) {
    c->frame = rect(frame.x + l, frame.y + t, frame.w - l - r, frame.h - t - b);
    c->layout();
  }
}

}  // namespace pos::ui
