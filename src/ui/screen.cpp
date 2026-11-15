#include "ui/screen.h"

#include <algorithm>

#include "ui/panels.h"

namespace pos::ui {

Screen::~Screen() {
  // Widgets must not call back into a dead screen while the tree is destroyed.
  if (root_) root_->screen_ = nullptr;
  for (auto& o : overlays_) o->screen_ = nullptr;
}

void Screen::setRoot(std::unique_ptr<Widget> root) {
  dirty_.clear();
  target_ = consumer_ = nullptr;
  toast_ = nullptr;
  overlays_.clear();
  if (root_) root_->screen_ = nullptr;
  root_ = std::move(root);
  root_->screen_ = this;
  relayout();
}

void Screen::relayout() {
  if (!root_) return;
  root_->frame = Rect{0, 0, kScreenW, kScreenH};
  root_->layout();
  for (auto& o : overlays_) {
    o->frame = Rect{0, 0, kScreenW, kScreenH};
    o->layout();
  }
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
    if (d.w == w && !d.hasRect) {
      if (static_cast<int>(h) > static_cast<int>(d.hint) && d.hint != Hint::Feedback) d.hint = h;
      return;
    }
  dirty_.push_back({w, h, false, Rect{}});
}

void Screen::invalidateRect(Rect r, Hint hint) {
  if (root_ && !r.empty()) dirty_.push_back({root_.get(), hint, true, r});
}

void Screen::forget(Widget* w) {
  if (consumer_ == w) consumer_ = nullptr;
  if (target_ == w) target_ = nullptr;
  if (toast_ == w) toast_ = nullptr;
  dirty_.erase(std::remove_if(dirty_.begin(), dirty_.end(), [w](const Dirty& d) { return d.w == w && !d.hasRect; }), dirty_.end());
}

void Screen::forgetTree(Widget* w) {
  auto inTree = [w](const Widget* x) {
    for (; x; x = x->parent())
      if (x == w) return true;
    return false;
  };
  if (consumer_ && inTree(consumer_)) consumer_ = nullptr;
  if (target_ && inTree(target_)) target_ = nullptr;
  if (toast_ && inTree(toast_)) toast_ = nullptr;
  dirty_.erase(std::remove_if(dirty_.begin(), dirty_.end(), [&](const Dirty& d) { return !d.hasRect && inTree(d.w); }), dirty_.end());
}

Widget* Screen::overlayOf(Widget* w) const {
  while (w->parent()) w = w->parent();
  for (auto& o : overlays_)
    if (o.get() == w) return w;
  return nullptr;
}

Widget* Screen::addOverlay(std::unique_ptr<Widget> w) {
  Widget* raw = w.get();
  w->screen_ = this;
  w->frame = Rect{0, 0, kScreenW, kScreenH};
  overlays_.push_back(std::move(w));
  raw->layout();
  markDirty(raw, Hint::Screen);
  return raw;
}

void Screen::closeOverlay(Widget* w) {
  for (size_t i = 0; i < overlays_.size(); ++i) {
    if (overlays_[i].get() != w) continue;
    const Rect f = w->frame;
    forgetTree(w);
    w->screen_ = nullptr;
    retired_.push_back(std::move(overlays_[i]));  // may be the widget whose handler is running
    overlays_.erase(overlays_.begin() + i);
    if (toast_ == w) toast_ = nullptr;
    invalidateRect(f, Hint::Screen);
    return;
  }
}

void Screen::hideToast() {
  if (timers_ && toastTimer_) timers_->cancel(toastTimer_);
  toastTimer_ = 0;
  if (toast_) closeOverlay(toast_);
  toast_ = nullptr;
}

void Screen::render(Canvas& canvas, DisplayManager& dm) {
  retired_.clear();
  if (dirty_.empty()) return;
  std::vector<Dirty> work;
  work.swap(dirty_);
  for (const Dirty& d : work) {
    if (!d.hasRect) {
      // A dirty ancestor redraws this widget anyway.
      bool covered = false;
      for (const Dirty& o : work)
        if (!o.hasRect && o.w != d.w && isAncestor(o.w, d.w)) covered = true;
      if (covered || !d.w->visible) continue;
    }
    const Rect f = d.hasRect ? d.rect : d.w->frame;
    canvas.setClipRect(f.x, f.y, f.w, f.h);
    if (!d.w->transparent) canvas.fillRect(f.x, f.y, f.w, f.h, d.hasRect ? paper : d.w->bg());
    d.w->drawTree(canvas, f);
    // Anything drawn in the root tree is covered again by the overlays above it.
    if (!overlayOf(d.w)) {
      for (auto& ov : overlays_) {
        const int x0 = std::max<int>(f.x, ov->frame.x), y0 = std::max<int>(f.y, ov->frame.y);
        const int x1 = std::min<int>(f.x + f.w, ov->frame.x + ov->frame.w), y1 = std::min<int>(f.y + f.h, ov->frame.y + ov->frame.h);
        if (x1 > x0 && y1 > y0)
          ov->drawTree(canvas, Rect{static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)});
      }
    }
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
  retired_.clear();
  if (!root_) return;
  switch (g.type) {
    case Gesture::Down:
      consumer_ = nullptr;
      target_ = nullptr;
      for (size_t i = overlays_.size(); i-- > 0 && !target_;) target_ = overlays_[i]->hit(g.x, g.y);
      if (!target_) {  // a modal overlay (Sheet) takes every touch outside itself
        for (size_t i = overlays_.size(); i-- > 0 && !target_;)
          if (overlays_[i]->modal) target_ = overlays_[i].get();
      }
      if (!target_) target_ = root_->hit(g.x, g.y);
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

namespace pos::ui {

void Screen::showToast(const std::string& text, uint32_t ms) {
  hideToast();
  toast_ = addOverlay(std::make_unique<Toast>(text));
  if (timers_) toastTimer_ = timers_->after(ms, [this] { toastTimer_ = 0; hideToast(); });
}

}  // namespace pos::ui
