#include "ui/widget.h"

#include <algorithm>

#include "ui/screen.h"

namespace pos::ui {

Widget::~Widget() {
  if (Screen* s = screen()) s->forget(this);
}

Widget* Widget::adopt(std::unique_ptr<Widget> child) {
  child->parent_ = this;
  children_.push_back(std::move(child));
  return children_.back().get();
}

std::unique_ptr<Widget> Widget::remove(Widget* child) {
  for (size_t i = 0; i < children_.size(); ++i) {
    if (children_[i].get() == child) {
      if (Screen* s = screen()) s->forgetTree(child);
      std::unique_ptr<Widget> out = std::move(children_[i]);
      children_.erase(children_.begin() + i);
      out->parent_ = nullptr;
      return out;
    }
  }
  return nullptr;
}

Screen* Widget::screen() const {
  for (const Widget* w = this; w; w = w->parent_)
    if (w->screen_) return w->screen_;
  return nullptr;
}

Canvas* Widget::canvas() const {
  Screen* s = screen();
  return s ? &s->canvas() : nullptr;
}

void Widget::invalidate(Hint h) {
  if (Screen* s = screen()) s->markDirty(this, h);
}

Color Widget::bg() const {
  for (const Widget* w = this; w; w = w->parent_)
    if (w->hasBg_) return w->bg_;
  return paper;
}

Widget* Widget::hit(int x, int y) {
  if (!visible || !touchable || !contains(x, y)) return nullptr;
  for (size_t i = children_.size(); i-- > 0;)
    if (Widget* h = children_[i]->hit(x, y)) return h;
  return this;
}

void Widget::drawTree(Canvas& c, Rect clip) {
  if (!visible || clip.empty()) return;
  c.setClipRect(clip.x, clip.y, clip.w, clip.h);
  draw(c);
  for (auto& ch : children_) {
    if (!ch->visible) continue;
    const int x0 = std::max<int>(clip.x, ch->frame.x), y0 = std::max<int>(clip.y, ch->frame.y);
    const int x1 = std::min<int>(clip.x + clip.w, ch->frame.x + ch->frame.w);
    const int y1 = std::min<int>(clip.y + clip.h, ch->frame.y + ch->frame.h);
    if (x1 <= x0 || y1 <= y0) continue;
    ch->drawTree(c, Rect{static_cast<int16_t>(x0), static_cast<int16_t>(y0), static_cast<int16_t>(x1 - x0),
                         static_cast<int16_t>(y1 - y0)});
  }
  c.setClipRect(clip.x, clip.y, clip.w, clip.h);
}

}  // namespace pos::ui
