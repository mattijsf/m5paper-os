// ListItem (96 px row) and Pager (page bar + swipe paging). docs/UI.md "Widgets", "Patterns": lists
// never scroll; a list longer than one screen is paged.
#pragma once

#include <functional>
#include <string>

#include "ui/controls.h"
#include "ui/layout.h"

namespace pos::ui {

// 96 px row: optional leading icon, title, optional subtitle, optional trailing element, divider below.
// Tapping the row fires onClick; pressed = inverted row.
class ListItem : public Pressable {
 public:
  enum class Trailing : uint8_t { None, Text, Chevron, Toggle, Badge };

  ListItem(std::string title = "", std::string subtitle = "", bool hasIcon = false, Icon icon = Icon::Check)
      : title_(std::move(title)), subtitle_(std::move(subtitle)), hasIcon_(hasIcon), icon_(icon) {}

  void setTitle(const std::string& t);
  void setSubtitle(const std::string& t);
  // The subtitle is what the user came to read (a message preview, a mail subject, a book's author): drawn in ink at the body size.
  // Default false: metadata (a state, a hint), drawn in `secondary` at the caption size.
  void setSubtitleIsContent(bool content);
  void setIcon(Icon i);
  void setTrailingText(const std::string& t);
  void setChevron();
  // Adds an on/off pill on the right; `onChange` fires when it is tapped (the row itself does not).
  void setToggle(bool on, std::function<void(bool)> onChange = {});
  void setBadge(int count);
  void clearTrailing();
  const std::string& title() const { return title_; }
  Toggle* toggle() const { return toggle_; }
  Badge* badge() const { return badge_; }

  Size measure(int availW) override { return {availW, 96}; }
  void layout() override;
  void draw(Canvas& c) override;

 private:
  void clearChildren();
  std::string title_, subtitle_, trailingText_;
  bool subtitleContent_ = false;
  bool hasIcon_;
  Icon icon_;
  Trailing trailing_ = Trailing::None;
  Toggle* toggle_ = nullptr;
  Badge* badge_ = nullptr;
};

class Pager;

// The "3 / 12" bar with previous and next targets (part of a Pager).
class PagerBar : public Widget {
 public:
  explicit PagerBar(Pager* owner) : owner_(owner) {}
  Size measure(int availW) override { return {availW, kTouchMin}; }
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;
  void forcePressed(int which) { pressedTarget_ = which; invalidate(Hint::Feedback); }  // -1 prev, 1 next, 0 none (tests)

 private:
  int targetAt(int x) const;  // -1 previous, 1 next, 0 none
  Pager* owner_;
  int pressedTarget_ = 0;
};

// Container: its content children fill the frame above a 72 px PagerBar. A tap on the bar's arrows
// or a horizontal swipe anywhere in the content changes the page and calls onPage(page): fill the
// content for that page there. Page numbers are 0-based; the bar shows them 1-based.
class Pager : public Widget {
 public:
  explicit Pager(int pages = 1, int page = 0);
  std::function<void(int)> onPage;
  int page() const { return page_; }
  int pages() const { return pages_; }
  void setPages(int n);
  // Changes the page (clamped), calls onPage and redraws the content and the bar.
  void setPage(int p);
  void next() { setPage(page_ + 1); }
  void prev() { setPage(page_ - 1); }
  PagerBar* bar() const { return bar_; }  // bar()->visible = false hides it (single page)

  Size measure(int availW) override;
  void layout() override;
  bool onTouch(const Gesture& g) override;

 protected:
  virtual void pageChanged() {}  // the page is another one: before onPage

 private:
  int page_, pages_;
  PagerBar* bar_;
};

// A list too long for one screen: `rowsPerPage` rows in a Pager. `fill(row, index)` makes a row show item `index`; it
// runs for the rows of the page on screen whenever the page or the items change. The bar shows only with a second page.
class PagedList : public Pager {
 public:
  using Fill = std::function<void(ListItem& row, int index)>;
  PagedList(int rowsPerPage, Fill fill);
  // How many items there are: the pages follow (the page on screen stays if it still exists) and the rows are filled.
  void setCount(int count);
  int count() const { return count_; }
  // Fills the rows on screen again: the items changed, their number did not.
  void refill();
  // Turns to the page that holds item `index`.
  void showItem(int index) { setPage(index / rowsPerPage()); }
  int rowsPerPage() const { return static_cast<int>(rows_.size()); }

 protected:
  void pageChanged() override { refill(); }

 private:
  Fill fill_;
  Column* list_;
  std::vector<ListItem*> rows_;
  int count_ = 0;
};

}  // namespace pos::ui
