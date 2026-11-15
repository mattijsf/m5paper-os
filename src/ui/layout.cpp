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

Size Row::measure(int availW) {
  int w = 0, h = 0, n = 0;
  for (auto& c : children()) {
    if (!c->visible) continue;
    Size s = c->measure(availW);
    w += s.w;
    h = std::max(h, s.h);
    ++n;
  }
  if (n > 1) w += gap * (n - 1);
  return {w, h};
}

void Row::layout() {
  std::vector<Widget*> kids;
  for (auto& c : children())
    if (c->visible) kids.push_back(c.get());
  int fixed = 0, flexSum = 0;
  std::vector<Size> sizes(kids.size());
  for (size_t i = 0; i < kids.size(); ++i) {
    sizes[i] = kids[i]->measure(frame.w);
    if (kids[i]->flex > 0) flexSum += kids[i]->flex;
    else fixed += sizes[i].w;
  }
  if (!kids.empty()) fixed += gap * static_cast<int>(kids.size() - 1);
  int left = std::max(0, frame.w - fixed), given = 0, flexSeen = 0, x = frame.x;
  for (size_t i = 0; i < kids.size(); ++i) {
    int w = sizes[i].w;
    if (kids[i]->flex > 0) {
      flexSeen += kids[i]->flex;
      int upto = left * flexSeen / flexSum;
      w = upto - given;
      given = upto;
    }
    int off, h;
    crossPlace(align, frame.h, sizes[i].h, off, h);
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
