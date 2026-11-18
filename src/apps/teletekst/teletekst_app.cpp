// Teletekst: NOS Teletekst, page by page (docs/apps/teletekst.md). One screen: the page as a grid of 40 x 24 characters in
// ink and paper, the page's four coloured links as buttons, previous / next page and subpage, a number pad.
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/teletekst/teletekst_model.h"
#include "core/app.h"
#include "core/fs.h"
#include "core/timefmt.h"
#include "core/utf8.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::teletekst {

namespace {

using ui::Type;

constexpr int kCellW = 13, kCellH = 26;      // a character of Type::Mono is 13 px wide
constexpr int kShownRows = kRows - 1;        // the last row holds the four coloured links: they are the buttons below
constexpr int kGridW = kCols * kCellW;       // 520
constexpr int kGridH = kShownRows * kCellH;  // 624
constexpr int kSideMargin = (kScreenW - kGridW) / 2;  // 10
constexpr int kInkBelow = 140;               // a pixel of a character darker than this is ink, the rest paper
constexpr int64_t kFreshSec = 120;           // a copy this young is shown without a read
constexpr int64_t kResumeSec = 3600;         // reopened within the hour: the page that was open, else 100
constexpr int kSavedPages = 16;              // copies kept on the flash (3-9 KB each)
constexpr const char* kCacheDir = "/data/cache/teletekst/";

std::string savedName(const PageRef& ref) { return "p" + ref.id(); }

// "702" or "702/2": how the page names itself.
std::string label(const PageRef& ref) {
  std::string s = std::to_string(ref.page);
  if (ref.sub > 0) s += "/" + std::to_string(ref.sub);
  return s;
}

// A coloured background that is dark on a television is an ink bar here; white, yellow and cyan are paper.
bool darkBackground(uint8_t bg) { return bg != Black && bg != White && bg != Yellow && bg != Cyan; }

using PagePtr = std::shared_ptr<const Page>;  // a Page is 8 KB: on the heap, never a local (the UI thread's stack is 8 KB)

// The page. Only ink and paper (docs/apps/teletekst.md "Colours"): a page turn is one Fast pass with nothing to settle.
class PageView : public ui::Widget {
 public:
  std::function<void(PageRef)> onLink;
  std::function<void(int)> onSwipe;  // -1: towards the previous page, +1: the next

  void set(PagePtr page) {
    page_ = std::move(page);
    invalidate(Hint::Page);
  }

  ui::Size measure(int) override { return {kGridW, kGridH}; }

  void draw(Canvas& c) override {
    if (!page_) return;
    const int x0 = frame.x + (frame.w - kGridW) / 2, y0 = frame.y;
    int32_t cx = 0, cy = 0, cw = 0, ch = 0;
    c.getClipRect(&cx, &cy, &cw, &ch);
    // Runs of characters are drawn as one string when the font keeps the grid (it does: checked once, not assumed).
    static const bool grid = ui::measureText(c, "MMMMMMMMMM", Type::Mono) == 10 * kCellW &&
                             ui::measureText(c, "iiii.iiiii", Type::Mono) == 10 * kCellW;
    const int textTop = (kCellH - ui::lineHeight(c, Type::Mono)) / 2;
    for (int r = 0; r < kShownRows; ++r) {
      const int y = y0 + r * kCellH;
      if (y + kCellH <= cy || y >= cy + ch) continue;
      const Cell* row = page_->cells[r];
      // Ink bars first.
      for (int col = 0; col < kCols;) {
        if (!darkBackground(row[col].bg)) {
          ++col;
          continue;
        }
        int end = col;
        while (end < kCols && darkBackground(row[end].bg)) ++end;
        c.fillRect(x0 + col * kCellW, y, (end - col) * kCellW, kCellH, ui::ink);
        col = end;
      }
      for (int col = 0; col < kCols; ++col)
        if (row[col].mosaic) drawMosaic(c, row[col], x0 + col * kCellW, y);
      // Text: runs of cells on the same ground, each clipped to its cells (the font's line is a little higher than a row).
      for (int col = 0; col < kCols;) {
        if (!hasGlyph(row[col])) {
          ++col;
          continue;
        }
        const bool dark = darkBackground(row[col].bg);
        int end = col;
        std::string text;
        while (end < kCols && !row[end].mosaic && darkBackground(row[end].bg) == dark && (grid || end == col)) {
          utf8::append(text, hasGlyph(row[end]) ? row[end].ch : ' ');
          ++end;
        }
        while (end > col + 1 && text.back() == ' ') {  // spaces at the end draw nothing
          text.pop_back();
          --end;
        }
        const int rx = x0 + col * kCellW, rw = (end - col) * kCellW;
        const int ax = std::max<int>(rx, cx), ay = std::max<int>(y, cy);
        const int bx = std::min<int>(rx + rw, cx + cw), by = std::min<int>(y + kCellH, cy + ch);
        if (bx > ax && by > ay) {
          c.setClipRect(ax, ay, bx - ax, by - ay);
          ui::drawText(c, text, rx, y + textTop, Type::Mono, dark ? ui::paper : ui::ink, ui::Anchor::TopLeft, dark ? ui::ink : ui::paper);
        }
        col = end;
      }
      c.setClipRect(cx, cy, cw, ch);
    }
    // The characters end as ink or paper with nothing between, like the bitmap letters of a television's teletext: the Fast
    // pass then shows the page as it is, with no dithered edges and nothing left to settle (docs/DISPLAY.md).
    if (auto* buf = static_cast<uint8_t*>(c.getBuffer())) {
      const int stride = c.width();
      const int ax = std::max<int>(x0, cx), bx = std::min<int>(x0 + kGridW, cx + cw);
      const int ay = std::max<int>(y0, cy), by = std::min<int>(y0 + kGridH, cy + ch);
      for (int y = ay; y < by; ++y) {
        uint8_t* p = buf + static_cast<size_t>(y) * stride;
        for (int x = ax; x < bx; ++x) p[x] = p[x] < kInkBelow ? 0 : 255;
      }
    }
  }

  bool onTouch(const Gesture& g) override {
    switch (g.type) {
      case Gesture::Down:
        return true;
      case Gesture::Tap: {
        const int col = (g.x - (frame.x + (frame.w - kGridW) / 2)) / kCellW, row = (g.y - frame.y) / kCellH;
        const PageRef to = page_ ? linkNear(*page_, row, std::max(0, std::min(col, kCols - 1))) : PageRef{};
        if (to.valid() && onLink) onLink(to);
        return true;
      }
      case Gesture::SwipeLeft:
      case Gesture::SwipeRight:
        if (onSwipe) onSwipe(g.type == Gesture::SwipeLeft ? 1 : -1);
        return true;
      default:
        return false;
    }
  }

 private:
  // A character that shows: not a mosaic, not a space, not in the colour of its own background (teletext hides text so).
  static bool hasGlyph(const Cell& cell) { return !cell.mosaic && cell.ch != ' ' && cell.fg != cell.bg; }

  // Six blocks, two across and three down. On paper the colour is a density of ink: white, yellow, cyan and blue solid,
  // red and magenta two rows of three, green every other row; on an ink bar the blocks are paper.
  static void drawMosaic(Canvas& c, const Cell& cell, int x, int y) {
    if (cell.ch == 0 || cell.fg == cell.bg) return;
    const bool dark = darkBackground(cell.bg);
    const int period = dark ? 1 : cell.fg == Green ? 2 : (cell.fg == Red || cell.fg == Magenta) ? 3 : 1;
    const Color color = dark ? ui::paper : ui::ink;
    static const int kX[3] = {0, kCellW / 2, kCellW};                         // 0, 6, 13
    static const int kY[4] = {0, kCellH / 3 + 1, kCellH - kCellH / 3 - 1, kCellH};  // 0, 9, 17, 26
    for (int bit = 0; bit < 6; ++bit) {
      if (!(cell.ch & (1 << bit))) continue;
      const int bx = x + kX[bit % 2], bw = kX[bit % 2 + 1] - kX[bit % 2];
      const int by = y + kY[bit / 2], bh = kY[bit / 2 + 1] - kY[bit / 2];
      if (period == 1) {
        c.fillRect(bx, by, bw, bh, color);
        continue;
      }
      for (int ly = by; ly < by + bh; ++ly)  // the lines follow the panel's rows, so neighbouring blocks join
        if (period == 2 ? ly % 2 == 0 : ly % 3 != 2) c.drawFastHLine(bx, ly, bw, color);
    }
  }

  PagePtr page_;
};

// Where the page is, or what is said while there is none: always the grid's height, so the buttons below stay put.
class Holder : public ui::Column {
 public:
  ui::Size measure(int availW) override { return {availW, kGridH}; }
};

// One of the page's four coloured links: an outlined button with a short word ("binnenland"), four to a row.
class FastButton : public ui::Pressable {
 public:
  void setText(const std::string& t) {
    if (t == text_) return;
    text_ = t;
    invalidate(Hint::Content);
  }
  void setEnabled(bool on) {
    if (on == enabled) return;
    enabled = on;
    invalidate(Hint::Content);
  }
  ui::Size measure(int) override { return {96, ui::kTouchMin}; }
  void draw(Canvas& c) override {
    if (text_.empty()) return;  // a page with fewer than four links
    const Color fg = pressed_ ? ui::paper : ui::ink, back = pressed_ ? ui::ink : ui::paper;
    if (pressed_) {
      c.fillRoundRect(frame.x, frame.y, frame.w, frame.h, ui::kRadius, ui::ink);
    } else {
      ui::strokeRound(c, frame, ui::kRadius, ui::kStroke, ui::ink);
    }
    const Type type = ui::measureText(c, text_, Type::Body) <= frame.w - 2 * ui::s1 ? Type::Body : Type::Caption;
    ui::drawText(c, ui::ellipsize(c, text_, type, frame.w - 2 * ui::s1), frame.x + frame.w / 2, frame.y + frame.h / 2, type, fg,
                 ui::Anchor::MiddleCenter, back);
  }

 private:
  std::string text_;
};

// Previous / next page: the page's number, nothing at all when there is no such page.
class PageButton : public ui::Button {
 public:
  PageButton(std::function<void()> fn) : ui::Button("", std::move(fn), ui::Button::Kind::Secondary) {}
  void draw(Canvas& c) override {
    if (!text().empty()) ui::Button::draw(c);
  }
};

// Previous / next subpage: an arrow, gray at the first and last one, nothing on a page without subpages.
class SubButton : public ui::IconButton {
 public:
  using ui::IconButton::IconButton;
  bool shown = false;
  void draw(Canvas& c) override {
    if (shown) ui::IconButton::draw(c);
  }
};

void setEnabled(ui::Pressable* w, bool on) {
  if (w->enabled == on) return;
  w->enabled = on;
  w->invalidate(Hint::Content);
}

class TeletekstScreen : public Screen {
 public:
  int sideMargin() const override { return kSideMargin; }

  void build(ui::Column& root) override {
    setTitle("Teletekst");
    setAction(Icon::RefreshCw, [this] {
      if (want_.valid()) open(want_, true);
    });
    holder_ = root.add<Holder>();
    root.add<ui::Spacer>(4);
    status_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    root.add<ui::Spacer>(6);

    auto* fast = root.add<ui::Row>(ui::s1);
    for (int i = 0; i < 4; ++i) {
      fast_[i] = fast->add<FastButton>();
      fast_[i]->flex = 1;
      fast_[i]->onClick = [this, i] {
        if (page_ && static_cast<size_t>(i) < page_->fast.size()) open(page_->fast[static_cast<size_t>(i)].to);
      };
    }
    root.add<ui::Spacer>(ui::s1);

    auto* nav = root.add<ui::Row>(ui::s1);
    prev_ = nav->add<PageButton>([this] { if (page_) open(page_->prev); });
    prevSub_ = nav->add<SubButton>(Icon::ChevronLeft);
    prevSub_->onClick = [this] { if (page_) open(page_->prevSub); };
    number_ = nav->add<ui::Button>("", [this] { openPad(); });
    number_->flex = 1;
    nextSub_ = nav->add<SubButton>(Icon::ChevronRight);
    nextSub_->onClick = [this] { if (page_) open(page_->nextSub); };
    next_ = nav->add<PageButton>([this] { if (page_) open(page_->next); });

    // The page to start on, and its saved copy before the first frame.
    PageRef start;
    const int64_t now = ctx().clock.now(), left = static_cast<int64_t>(ctx().store.get("left", 0.0));
    if (!(now >= left && now - left < kResumeSec && parseRef(ctx().store.get("page", "100"), start))) start = PageRef{kFirstPage, 0};
    want_ = start;
    int64_t fetched = 0;
    if (PagePtr saved = loadSaved(start, fetched)) {
      page_ = saved;
      fetched_ = fetched;
    }
    fillHolder();
    showControls();
    showStatus();
  }

  void onShow() override {
    if (!page_ || !fresh()) fetch(want_);
  }

  void onHide() override {
    ctx().store.set("page", (page_ ? page_->self : want_).id());
    ctx().store.set("left", static_cast<double>(ctx().clock.now()));
  }

  std::string debugState() const override {
    return "page=" + (page_ ? label(page_->self) : std::string("none")) + "|want=" + label(want_) + "|" + status_->text() +
           (busy_ ? "|busy" : "") + (error_.empty() ? "" : "|error=" + error_);
  }

 private:
  bool fresh() const {
    const int64_t now = ctx().clock.now();
    return page_ && fetched_ > 0 && now >= fetched_ && now - fetched_ < kFreshSec;
  }

  // The saved copy of a page, null if there is none (or it no longer parses).
  static PagePtr loadSaved(const PageRef& ref, int64_t& fetched) {
    std::string saved, json;
    if (!fs::readFile(kCacheDir + savedName(ref), saved) || !unpackSaved(saved, fetched, json)) return nullptr;
    auto page = std::make_shared<Page>();
    if (!parsePage(json, ref, *page)) return nullptr;
    return page;
  }

  // The page view, or what is said instead of it while there is no page.
  void fillHolder() {
    holder_->removeAll();
    view_ = nullptr;
    if (page_) {
      view_ = holder_->add<PageView>();
      view_->onLink = [this](PageRef to) { open(to); };
      view_->onSwipe = [this](int dir) { turn(dir); };
      view_->set(page_);
    } else {
      holder_->add<ui::Spacer>(kGridH / 5);
      if (error_.empty()) {
        holder_->add<ui::EmptyState>(Icon::Tv, "Loading...");
      } else {
        holder_->add<ui::EmptyState>(Icon::WifiOff, "Teletekst not loaded", error_, "Retry", [this] { open(want_, true); });
      }
      holder_->add<ui::Spacer>(0, 1);
    }
    if (holder_->host()) holder_->host()->relayout();
  }

  void showControls() {
    number_->setText(label(busy_ || !page_ ? want_ : page_->self));
    const PageRef prev = page_ ? page_->prev : PageRef{}, next = page_ ? page_->next : PageRef{};
    prev_->setText(prev.valid() ? std::to_string(prev.page) : "");
    next_->setText(next.valid() ? std::to_string(next.page) : "");
    setEnabled(prev_, prev.valid());
    setEnabled(next_, next.valid());
    const bool subpages = page_ && page_->hasSubpages();
    for (SubButton* b : {prevSub_, nextSub_}) {
      const bool on = page_ && (b == prevSub_ ? page_->prevSub : page_->nextSub).valid();
      if (b->shown == subpages && b->enabled == on) continue;
      b->shown = subpages;
      b->enabled = on;
      b->invalidate(Hint::Content);
    }
    const size_t links = page_ ? page_->fast.size() : 0;
    for (size_t i = 0; i < 4; ++i) {
      fast_[i]->setText(i < links ? page_->fast[i].title : "");
      fast_[i]->setEnabled(i < links);
    }
  }

  void showStatus() {
    const std::string when = clockText(fetched_);
    if (busy_) {
      status_->setText("Loading " + label(want_) + "...");
    } else if (!page_) {
      status_->setText("");
    } else if (!error_.empty()) {
      status_->setText("Saved" + (when.empty() ? std::string(" copy") : " " + when) + ": " + error_);
    } else {
      status_->setText(when.empty() ? "NOS Teletekst" : "Updated " + when);
    }
  }

  // A swipe: the next or previous subpage while there is one, else the next or previous page.
  void turn(int dir) {
    if (!page_) return;
    const PageRef sub = dir > 0 ? page_->nextSub : page_->prevSub, other = dir > 0 ? page_->next : page_->prev;
    open(sub.valid() ? sub : other);
  }

  // Goes to a page: a young saved copy at once, else a read (the copy comes back if the read fails).
  void open(PageRef ref, bool force = false) {
    if (!ref.valid()) return;
    want_ = ref;
    if (!force) {
      int64_t fetched = 0;
      const int64_t now = ctx().clock.now();
      PagePtr saved = loadSaved(ref, fetched);
      if (saved && now >= fetched && now - fetched < kFreshSec) {
        ++seq_;  // an answer still on its way is for a page that was left
        busy_ = false;
        show(saved, fetched);
        return;
      }
    }
    fetch(ref);
  }

  void show(PagePtr p, int64_t fetched) {
    const bool had = static_cast<bool>(page_);
    page_ = std::move(p);
    fetched_ = fetched;
    error_.clear();
    want_ = page_->self;
    if (had && view_) {
      view_->set(page_);
    } else {
      fillHolder();
    }
    showControls();
    showStatus();
  }

  void fetch(PageRef ref) {
    const unsigned seq = ++seq_;
    busy_ = true;
    showControls();
    showStatus();
    hal::HttpRequest req;
    req.url = pageUrl(ref);
    req.timeoutMs = 10000;
    ctx().http(req, [this, ref, seq](hal::HttpResponse r) {
      if (seq != seq_) return;  // another page was asked for meanwhile
      busy_ = false;
      auto p = std::make_shared<Page>();
      if (r.status == 200 && r.body.size() <= kMaxBodyBytes && parsePage(r.body, ref, *p)) {
        const int64_t now = ctx().clock.now();
        ctx().log("page %s: %u bytes", ref.id().c_str(), static_cast<unsigned>(r.body.size()));
        save(ref, packSaved(now, r.body));
        show(p, now);
        return;
      }
      const bool missing = r.status == 404;
      const std::string why = missing ? "Page " + label(ref) + " does not exist"
                              : r.status == 0 ? (r.error.empty() ? std::string("No connection") : r.error)
                              : r.status == 200 ? std::string("Unexpected answer")
                              : "NOS answered " + std::to_string(r.status);
      ctx().log("page %s not read: status %d", ref.id().c_str(), r.status);
      failed(ref, why, missing);
    });
  }

  // A read failed. The saved copy of that page if there is one (not for a page that does not exist), else the page on
  // screen stays and a toast says why; with nothing on screen the reason takes the page's place.
  void failed(const PageRef& ref, const std::string& why, bool missing) {
    int64_t fetched = 0;
    PagePtr saved = missing ? nullptr : loadSaved(ref, fetched);
    if (saved) {
      show(saved, fetched);
      error_ = why;
      showStatus();
      return;
    }
    if (page_) {
      want_ = page_->self;
      showControls();
      showStatus();
      ctx().toast(why);
      return;
    }
    error_ = why;
    fillHolder();
    showControls();
    showStatus();
  }

  // Keeps the copy on the flash and drops the oldest when more than kSavedPages are kept. On the worker.
  void save(const PageRef& ref, std::string packed) {
    std::vector<std::string> names;
    const std::string list = ctx().store.get("saved", "");
    for (size_t a = 0; a < list.size();) {
      size_t b = list.find(',', a);
      if (b == std::string::npos) b = list.size();
      if (b > a) names.push_back(list.substr(a, b - a));
      a = b + 1;
    }
    const std::string name = savedName(ref);
    names.erase(std::remove(names.begin(), names.end(), name), names.end());
    names.push_back(name);
    std::vector<std::string> drop;
    while (names.size() > static_cast<size_t>(kSavedPages)) {
      drop.push_back(names.front());
      names.erase(names.begin());
    }
    std::string joined;
    for (const std::string& n : names) joined += (joined.empty() ? "" : ",") + n;
    ctx().store.set("saved", joined);
    ctx().background([name, packed = std::move(packed), drop] {
      fs::writeFile(kCacheDir + name, packed);
      for (const std::string& d : drop) fs::removeAll(kCacheDir + d);
    });
  }

  // The number pad: three digits and the page opens.
  void openPad() {
    digits_.clear();
    ui::openSheet(*number_, [this](ui::Sheet& s) {
      // Its own column: the sheet's 16 px between rows would not leave room for four rows of keys.
      ui::Column* col = s.content()->add<ui::Column>(ui::s1);
      auto* head = col->add<ui::Row>(ui::s2);
      head->add<ui::Label>("Go to page", Type::Title);
      padLabel_ = head->add<ui::Label>(padText(), Type::Headline);
      padLabel_->align = ui::TextAlign::Right;
      padLabel_->flex = 1;
      ui::Sheet* sheet = &s;
      auto key = [this, sheet](ui::Row* row, const std::string& text) {
        auto* b = row->add<ui::Button>(text, [this, sheet, text] { padDigit(*sheet, text[0]); }, ui::Button::Kind::Secondary);
        b->flex = 1;
      };
      static const char* const kKeys[3][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}};
      for (const auto& line : kKeys) {
        auto* row = col->add<ui::Row>(ui::s1);
        for (const char* k : line) key(row, k);
      }
      auto* row = col->add<ui::Row>(ui::s1);
      auto* cancel = row->add<ui::Button>("Cancel", s.closing(), ui::Button::Kind::Text);
      cancel->flex = 1;
      key(row, "0");
      auto* del = row->add<ui::Button>("Delete", [this] {
        if (digits_.empty() || !padLabel_) return;
        digits_.pop_back();
        padLabel_->setText(padText(), Hint::Feedback);
      }, ui::Button::Kind::Text);
      del->flex = 1;
      s.onClose = [this] { padLabel_ = nullptr; };
    });
  }

  std::string padText() const {
    std::string s;
    for (size_t i = 0; i < 3; ++i) {
      if (i) s += "  ";
      s += i < digits_.size() ? digits_[i] : '_';
    }
    return s;
  }

  void padDigit(ui::Sheet& sheet, char d) {
    if (digits_.empty() && (d < '1' || d > '8')) return;  // pages are 100 to 899
    if (digits_.size() >= 3) return;
    digits_ += d;
    if (digits_.size() < 3) {
      if (padLabel_) padLabel_->setText(padText(), Hint::Feedback);
      return;
    }
    PageRef to;
    const bool ok = parseRef(digits_, to);
    sheet.close();
    if (ok) open(to);
  }

  PagePtr page_;           // the page on screen, null while there is none
  PageRef want_;           // the page asked for (the one on screen once it arrived)
  int64_t fetched_ = 0;    // when the page on screen was read
  std::string error_;      // why the last read failed, "" if it did not
  bool busy_ = false;
  unsigned seq_ = 0;
  std::string digits_;
  Holder* holder_ = nullptr;
  PageView* view_ = nullptr;
  ui::Label* status_ = nullptr;
  ui::Label* padLabel_ = nullptr;
  FastButton* fast_[4] = {};
  PageButton* prev_ = nullptr;
  PageButton* next_ = nullptr;
  ui::Button* number_ = nullptr;
  SubButton* prevSub_ = nullptr;
  SubButton* nextSub_ = nullptr;
};

class TeletekstApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<TeletekstScreen>(); }
};

}  // namespace

POS_REGISTER_APP(TeletekstApp, {
    .id = "teletekst",
    .name = "Teletekst",
    .icon = Icon::Teletekst,
    .order = 99,  // after the weather: the tiles of the other apps keep their places
});

}  // namespace pos::teletekst
