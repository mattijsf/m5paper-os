#include "ui/screen.h"

#include <algorithm>

namespace pos::ui {

Screen::~Screen() {
  // Widgets must not call back into a dead screen while the tree is destroyed.
  if (root_) root_->screen_ = nullptr;
}

void Screen::setRoot(std::unique_ptr<Widget> root) {
  dirty_.clear();
  target_ = consumer_ = nullptr;
  if (root_) root_->screen_ = nullptr;
  root_ = std::move(root);
  root_->screen_ = this;
  relayout();
}

void Screen::relayout() {
  if (!root_) return;
  root_->frame = Rect{0, 0, kScreenW, kScreenH};
  root_->layout();
  dirty_.clear();
  markDirty(root_.get(), Hint::Screen);
}

bool Screen::isAncestor(const Widget* a, const Widget* b) {
  for (const Widget* w = b->parent(); w; w = w->parent())
    if (w == a) return true;
  return false;
}

void Screen::markDirty(Widget* w, Hint h) {
  for (auto& d : dirty_)
    if (d.w == w) {
      if (static_cast<int>(h) > static_cast<int>(d.hint) && d.hint != Hint::Feedback) d.hint = h;
      return;
    }
  dirty_.push_back({w, h});
}

void Screen::forget(Widget* w) {
  if (consumer_ == w) consumer_ = nullptr;
  if (target_ == w) target_ = nullptr;
  dirty_.erase(std::remove_if(dirty_.begin(), dirty_.end(), [w](const Dirty& d) { return d.w == w; }), dirty_.end());
}

void Screen::forgetTree(Widget* w) {
  auto inTree = [w](const Widget* x) {
    for (; x; x = x->parent())
      if (x == w) return true;
    return false;
  };
  if (consumer_ && inTree(consumer_)) consumer_ = nullptr;
  if (target_ && inTree(target_)) target_ = nullptr;
  dirty_.erase(std::remove_if(dirty_.begin(), dirty_.end(), [&](const Dirty& d) { return inTree(d.w); }), dirty_.end());
}

void Screen::render(Canvas& canvas, DisplayManager& dm) {
  if (dirty_.empty()) return;
  std::vector<Dirty> work;
  work.swap(dirty_);
  for (const Dirty& d : work) {
    // A dirty ancestor redraws this widget anyway.
    bool covered = false;
    for (const Dirty& o : work)
      if (o.w != d.w && isAncestor(o.w, d.w)) covered = true;
    if (covered || !d.w->visible) continue;
    const Rect f = d.w->frame;
    canvas.setClipRect(f.x, f.y, f.w, f.h);
    canvas.fillRect(f.x, f.y, f.w, f.h, d.w->bg());
    d.w->drawTree(canvas, f);
    canvas.clearClipRect();
    dm.invalidate(f, d.hint);
  }
}

bool Screen::deliver(Widget* start, const Gesture& g) {
  for (Widget* w = start; w; w = w->parent())
    if (w->onTouch(g)) {
      if (g.type == Gesture::Down) consumer_ = w;
      return true;
    }
  return false;
}

void Screen::onGesture(const Gesture& g) {
  if (!root_) return;
  switch (g.type) {
    case Gesture::Down:
      consumer_ = nullptr;
      target_ = root_->hit(g.x, g.y);
      if (target_) deliver(target_, g);
      break;
    case Gesture::Tap:
    case Gesture::LongPress:
    case Gesture::Cancel:
      // The widget that took Down gets the rest of the touch (it may have been removed meanwhile).
      if (consumer_) consumer_->onTouch(g);
      if (g.type != Gesture::Cancel) consumer_ = target_ = nullptr;
      break;
    default:  // swipes bubble from the consumer, or from the widget under the finger at Down
      deliver(consumer_ ? consumer_ : target_, g);
      consumer_ = target_ = nullptr;
      break;
  }
}

}  // namespace pos::ui
