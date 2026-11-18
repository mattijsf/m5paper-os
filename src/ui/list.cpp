#include "ui/list.h"

#include <algorithm>

#include "core/timers.h"
#include "ui/root.h"

namespace pos::ui {

// ---- ListItem

void ListItem::setTitle(const std::string& t) { if (t != title_) { title_ = t; invalidate(Hint::Content); } }
void ListItem::setSubtitle(const std::string& t) { if (t != subtitle_) { subtitle_ = t; invalidate(Hint::Content); } }
void ListItem::setSubtitleIsContent(bool content) {
  if (content == subtitleContent_) return;
  subtitleContent_ = content;
  invalidate(Hint::Content);
}
void ListItem::setIcon(Icon i) {
  if (hasIcon_ && icon_ == i) return;
  hasIcon_ = true;
  icon_ = i;
  invalidate(Hint::Content);
}

void ListItem::clearChildren() {
  if (toggle_) remove(toggle_);
  if (badge_) remove(badge_);
  toggle_ = nullptr;
  badge_ = nullptr;
}

void ListItem::clearTrailing() {
  clearChildren();
  trailing_ = Trailing::None;
  invalidate(Hint::Content);
}

void ListItem::setTrailingText(const std::string& t) {
  if (trailing_ == Trailing::Text && trailingText_ == t) return;  // a list filled again must not redraw rows that stay as they are
  clearChildren();
  trailing_ = Trailing::Text;
  trailingText_ = t;
  invalidate(Hint::Content);
}

void ListItem::setChevron() {
  clearChildren();
  trailing_ = Trailing::Chevron;
  invalidate(Hint::Content);
}

void ListItem::setToggle(bool on, std::function<void(bool)> onChange) {
  clearChildren();
  trailing_ = Trailing::Toggle;
  toggle_ = add<Toggle>(on);
  toggle_->onChange = std::move(onChange);
  layout();
  invalidate(Hint::Content);
}

void ListItem::setBadge(int count) {
  if (trailing_ != Trailing::Badge) {
    clearChildren();
    trailing_ = Trailing::Badge;
    badge_ = add<Badge>(count);
    layout();
  } else {
    badge_->setCount(count);
    layout();  // "9" to "10" is a wider pill: the title's room follows
  }
  invalidate(Hint::Content);
}

void ListItem::layout() {
  if (toggle_) toggle_->frame = Rect{static_cast<int16_t>(frame.x + frame.w - 88), static_cast<int16_t>(frame.y + (frame.h - kTouchMin) / 2), 88, kTouchMin};
  if (badge_) {
    Size s = badge_->measure(frame.w);
    badge_->frame = Rect{static_cast<int16_t>(frame.x + frame.w - s.w), static_cast<int16_t>(frame.y + (frame.h - s.h) / 2),
                         static_cast<int16_t>(s.w), static_cast<int16_t>(s.h)};
  }
}

void ListItem::draw(Canvas& c) {
  const Color back = pressed_ ? ink : bg();
  const Color fg = pressed_ ? paper : ink;
  const Color sub = pressed_ ? paper : secondary;
  if (pressed_) c.fillRect(frame.x, frame.y, frame.w, frame.h, ink);
  const int cy = frame.y + frame.h / 2;
  int left = frame.x;
  if (hasIcon_) {
    drawIcon(c, icon_, frame.x + 16, cy, 32, fg, back);
    left = frame.x + 32 + s2;
  }
  int right = frame.x + frame.w;
  if (trailing_ == Trailing::Chevron) {
    drawIcon(c, Icon::ChevronRight, right - 16, cy, 32, pressed_ ? paper : secondary, back);
    right -= 32 + s1;
  } else if (trailing_ == Trailing::Text) {
    const int w = measureText(c, trailingText_, Type::Body);
    drawText(c, trailingText_, right, cy, Type::Body, sub, Anchor::MiddleRight, back);
    right -= w + s2;
  } else if (trailing_ == Trailing::Toggle) {
    right -= 88 + s2;
  } else if (trailing_ == Trailing::Badge && badge_) {
    right -= badge_->frame.w + s2;
  }
  const int textW = std::max(0, right - left);
  if (subtitle_.empty()) {
    drawText(c, ellipsize(c, title_, Type::BodyStrong, textW), left, cy, Type::BodyStrong, fg, Anchor::MiddleLeft, back);
  } else {
    const Type subType = subtitleContent_ ? Type::Body : Type::Caption;
    drawText(c, ellipsize(c, title_, Type::BodyStrong, textW), left, cy - 15, Type::BodyStrong, fg, Anchor::MiddleLeft, back);
    drawText(c, ellipsize(c, subtitle_, subType, textW), left, cy + 17, subType, subtitleContent_ ? fg : sub, Anchor::MiddleLeft, back);
  }
  c.drawFastHLine(frame.x, frame.y + frame.h - 1, frame.w, pressed_ ? ink : divider);
}

// ---- PagerBar

int PagerBar::targetAt(int x) const {
  if (x < frame.x + kTouchMin) return -1;
  if (x >= frame.x + frame.w - kTouchMin) return 1;
  return 0;
}

bool PagerBar::onTouch(const Gesture& g) {
  const int t = targetAt(g.startX);
  // The pressed look is drawn only if the finger is still down after kPressDelayMs: a quick tap goes straight to the new
  // page (one pass, and no waveform of a pressed arrow still running when the page is pushed: that cost 150 ms on the panel).
  auto release = [this] {
    downTarget_ = 0;
    ++pressSeq_;
    if (pressedTarget_ == 0) return;
    pressedTarget_ = 0;
    invalidate(Hint::Feedback);
  };
  switch (g.type) {
    case Gesture::Down: {
      if (t == 0 || (t < 0 && owner_->page() == 0) || (t > 0 && owner_->page() >= owner_->pages() - 1)) return false;  // the Pager takes swipes
      downTarget_ = t;
      pos::Timers* timers = host() ? host()->timers() : nullptr;
      if (!timers) {
        pressedTarget_ = t;
        invalidate(Hint::Feedback);
        return true;
      }
      const uint32_t seq = ++pressSeq_;
      timers->after(kPressDelayMs, [this, seq, t, alive = std::weak_ptr<bool>(alive_)] {
        auto a = alive.lock();
        if (!a || !*a || seq != pressSeq_) return;
        pressedTarget_ = t;
        invalidate(Hint::Feedback);
      });
      return true;
    }
    case Gesture::Tap:
      if (downTarget_ == 0) return false;
      release();
      if (t < 0) owner_->prev();
      else owner_->next();
      return true;
    case Gesture::LongPress:
    case Gesture::Cancel:
      if (downTarget_ == 0) return false;
      release();
      return true;
    default:
      return false;
  }
}

void PagerBar::draw(Canvas& c) {
  const int cy = frame.y + frame.h / 2;
  const bool canPrev = owner_->page() > 0, canNext = owner_->page() < owner_->pages() - 1;
  auto arrow = [&](int cx, Icon icon, bool can, bool pressed) {
    if (pressed) c.fillRoundRect(cx - kTouchMin / 2, frame.y, kTouchMin, kTouchMin, kRadius, ink);
    drawIcon(c, icon, cx, cy, 32, pressed ? paper : (can ? ink : disabled), pressed ? ink : bg());
  };
  arrow(frame.x + kTouchMin / 2, Icon::ChevronLeft, canPrev, pressedTarget_ < 0);
  arrow(frame.x + frame.w - kTouchMin / 2, Icon::ChevronRight, canNext, pressedTarget_ > 0);
  drawText(c, std::to_string(owner_->page() + 1) + " / " + std::to_string(owner_->pages()), frame.x + frame.w / 2, cy, Type::Body,
           ink, Anchor::MiddleCenter, bg());
}

// ---- Pager

Pager::Pager(int pages, int page) : pages_(std::max(1, pages)) {
  page_ = std::max(0, std::min(page, pages_ - 1));
  bar_ = add<PagerBar>(this);
}

void Pager::setPages(int n) {
  pages_ = std::max(1, n);
  bar_->invalidate(Hint::Content);
  if (page_ >= pages_) setPage(pages_ - 1);  // the page that was shown is gone: the last one, told to onPage like any change
}

void Pager::setPage(int p) {
  p = std::max(0, std::min(p, pages_ - 1));
  if (p == page_) return;
  page_ = p;
  pageChanged();
  if (onPage) onPage(page_);
  for (auto& ch : children()) ch->invalidate(Hint::Content);
}

Size Pager::measure(int availW) {
  Size m{availW, bar_->visible ? kTouchMin : 0};
  for (auto& ch : children())
    if (ch.get() != bar_ && ch->visible) m.h += ch->measure(availW).h;
  return m;
}

void Pager::layout() {
  const int barH = bar_->visible ? kTouchMin : 0;
  const int contentH = std::max(0, frame.h - barH);
  for (auto& ch : children()) {
    if (ch.get() == bar_) continue;
    ch->frame = Rect{frame.x, frame.y, frame.w, static_cast<int16_t>(contentH)};
    ch->layout();
  }
  bar_->frame = Rect{frame.x, static_cast<int16_t>(frame.y + contentH), frame.w, static_cast<int16_t>(barH)};
}

// ---- PagedList

PagedList::PagedList(int rowsPerPage, Fill fill) : fill_(std::move(fill)) {
  list_ = add<Column>(0);
  for (int i = 0; i < std::max(1, rowsPerPage); ++i) rows_.push_back(list_->add<ListItem>());
  for (ListItem* row : rows_) row->visible = false;
  bar()->visible = false;
}

void PagedList::setCount(int count) {
  count_ = std::max(0, count);
  const int perPage = rowsPerPage();
  setPages((count_ + perPage - 1) / perPage);  // (a page that is gone turns to the last one, which fills the rows)
  bar()->visible = pages() > 1;
  refill();
}

void PagedList::refill() {
  const int first = page() * rowsPerPage();
  bool cameOrWent = false;
  for (int i = 0; i < rowsPerPage(); ++i) {
    const int index = first + i;
    const bool show = index < count_;
    cameOrWent = cameOrWent || rows_[i]->visible != show;
    rows_[i]->visible = show;
    if (show) fill_(*rows_[i], index);
  }
  if (list_->frame.w > 0) list_->layout();  // rows came and went: positions change
  if (cameOrWent) list_->invalidate(Hint::Content);  // (a row that went leaves no picture behind)
}

bool Pager::onTouch(const Gesture& g) {
  if (g.type == Gesture::SwipeLeft) {
    next();
    return true;
  }
  if (g.type == Gesture::SwipeRight) {
    prev();
    return true;
  }
  return false;
}

}  // namespace pos::ui
