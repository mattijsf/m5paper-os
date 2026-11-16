// Read: the e-book reader (docs/apps/reader.md). Page screen with touch zones, footer and menu; a book is opened and
// indexed on the worker; the reading position survives restarts (progress.json in the book's cache folder).
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>

#include "apps/reader/book.h"
#include "apps/reader/library.h"
#include "core/app.h"
#include "core/fs.h"
#include "core/os.h"
#include "ui/root.h"
#include "ui/textblock.h"
#include "ui/widgets.h"

namespace pos::reader {

namespace {

using ui::Type;

constexpr int kMarginX = 40, kMarginTop = 48, kFooterH = 56;
constexpr int kPageW = kScreenW - 2 * kMarginX;
constexpr int kPageH = kScreenH - kMarginTop - kFooterH;

constexpr Type kFontTypes[4] = {Type::ReadS, Type::ReadM, Type::ReadL, Type::ReadXL};

ui::TextStyle pageStyle(int fontIndex) {
  ui::TextStyle s;
  s.body = kFontTypes[std::min(3, std::max(0, fontIndex))];
  s.width = kPageW;
  s.height = kPageH;
  return s;
}

// Shows the page; turns pages by touch zones (right 40% / swipe left = next, left 25% / swipe right = back,
// centre = menu). Also shows a one-line message instead of text (preparing, errors).
class PageView : public ui::TextBlock {
 public:
  enum class Zone { Prev, Next, Menu };
  std::function<void(Zone)> onZone;
  void setMessage(const std::string& m) {
    message_ = m;
    invalidate(Hint::Content);
  }
  bool onTouch(const Gesture& g) override {
    switch (g.type) {
      case Gesture::Down:
      case Gesture::Cancel:
      case Gesture::LongPress:
        return true;
      case Gesture::Tap: {
        const int pct = (g.startX - frame.x) * 100 / std::max(1, static_cast<int>(frame.w));
        if (onZone) onZone(pct < 25 ? Zone::Prev : pct >= 60 ? Zone::Next : Zone::Menu);
        return true;
      }
      case Gesture::SwipeLeft:
        if (onZone) onZone(Zone::Next);
        return true;
      case Gesture::SwipeRight:
        if (onZone) onZone(Zone::Prev);
        return true;
      default:
        return false;
    }
  }
  void draw(Canvas& c) override {
    if (!message_.empty()) {
      c.fillRect(frame.x, frame.y, frame.w, frame.h, bg());  // TextBlock is transparent: it clears its own frame
      ui::drawText(c, message_, frame.x + frame.w / 2, frame.y + frame.h / 2, Type::Body, ui::ink, ui::Anchor::MiddleCenter, bg());
      return;
    }
    TextBlock::draw(c);
  }

 private:
  std::string message_;
};

// Chapter title on the left, "12 / 340" on the right, a low-battery icon at the far right. Forwards touches to the page.
class PageFooter : public ui::Widget {
 public:
  Widget* forwardTo = nullptr;
  void set(const std::string& left, const std::string& right, bool lowBattery) {
    if (left == left_ && right == right_ && lowBattery == low_) return;
    left_ = left;
    right_ = right;
    low_ = lowBattery;
    invalidate(Hint::Page);  // with the page: one refresh per turn. As Content the number changed twice (Fast, then its
                             // settle sweep 0.6 s later) while the page text changed once
  }
  ui::Size measure(int availW) override { return {availW, kFooterH}; }
  bool onTouch(const Gesture& g) override { return forwardTo ? forwardTo->onTouch(g) : false; }
  void draw(Canvas& c) override {
    const int cy = frame.y + kFooterH / 2 - 4;
    int right = frame.x + frame.w - kMarginX;
    if (low_) {
      ui::drawIcon(c, Icon::BatteryLow, right - 16, cy, 32, ui::ink, bg());
      right -= 44;
    }
    ui::drawText(c, right_, right, cy, Type::Caption, ui::ink, ui::Anchor::MiddleRight, bg());
    const int rw = ui::measureText(c, right_, Type::Caption);
    const int room = right - rw - 24 - (frame.x + kMarginX);
    if (room > 40) ui::drawText(c, ui::ellipsize(c, left_, Type::Caption, room), frame.x + kMarginX, cy, Type::Caption, ui::ink, ui::Anchor::MiddleLeft, bg());
  }

 private:
  std::string left_, right_;
  bool low_ = false;
};

struct ContentsEntry {
  std::string title;
  int page;  // first page of the chapter (0-based)
};

// The chapter list: seven rows per page, the chapter being read is marked; a tap jumps to its first page.
class ContentsScreen : public Screen {
 public:
  ContentsScreen(std::vector<ContentsEntry> entries, int currentPage, std::function<void(int)> pick)
      : entries_(std::move(entries)), currentPage_(currentPage), pick_(std::move(pick)) {}

  void build(ui::Column& root) override {
    setTitle("Contents");
    const int n = static_cast<int>(entries_.size());
    const int pages = std::max(1, (n + kRows - 1) / kRows);
    // Open on the page that holds the chapter being read.
    int startPage = 0;
    for (int i = 0; i < n; ++i)
      if (entries_[i].page <= currentPage_) startPage = i / kRows;
    root.add<ui::Spacer>(ui::s2);
    auto* pager = root.add<ui::Pager>(pages, startPage);
    pager->flex = 1;
    pager->bar()->visible = pages > 1;
    auto* list = pager->add<ui::Column>(0);
    for (int i = 0; i < kRows; ++i) rows_.push_back(list->add<ui::ListItem>());
    pager->onPage = [this](int page) { fill(page); };
    fill(startPage);
  }

 private:
  static constexpr int kRows = 7;
  void fill(int page) {
    for (int i = 0; i < kRows; ++i) {
      const int idx = page * kRows + i;
      const bool show = idx < static_cast<int>(entries_.size());
      rows_[i]->visible = show;
      if (!show) continue;
      rows_[i]->setTitle(entries_[idx].title);
      rows_[i]->setTrailingText("p. " + std::to_string(entries_[idx].page + 1));
      const bool current = entries_[idx].page <= currentPage_ && (idx + 1 >= static_cast<int>(entries_.size()) || entries_[idx + 1].page > currentPage_);
      rows_[i]->setSubtitle(current ? "reading now" : "");
      rows_[i]->onClick = [this, p = entries_[idx].page] { pick_(p); };
    }
  }
  std::vector<ContentsEntry> entries_;
  int currentPage_;
  std::function<void(int)> pick_;
  std::vector<ui::ListItem*> rows_;
};

// 10 20 ... 100 under the segments of the "Go to" bar (same geometry as ui::LevelBar::draw): each box is "this far into the book".
class SegmentLabels : public ui::Widget {
 public:
  ui::Size measure(int availW) override { return {availW, 24}; }
  void draw(Canvas& c) override {
    const int gap = 6;
    const int segW = (frame.w - 9 * gap) / 10;
    const int x0 = frame.x + (frame.w - (10 * segW + 9 * gap)) / 2;
    for (int i = 0; i < 10; ++i)
      ui::drawText(c, std::to_string((i + 1) * 10), x0 + i * (segW + gap) + segW / 2, frame.y + frame.h / 2, Type::Caption, ui::secondary, ui::Anchor::MiddleCenter, bg());
  }
};

class PageScreen : public Screen {
 public:
  PageScreen(std::string file, int fontIndex) : file_(std::move(file)), font_(fontIndex) {}
  // The book stays "open" in the store until the reader leaves it for the library: after a power-off the app opens it again.
  void leaveBook() { ctx().store.set("open", std::string()); }
  ~PageScreen() override {
    if (steps_) steps_->cancel = true;  // the worker stops indexing a book nobody waits for
    saveProgress();
  }

  bool fullScreen() const override { return true; }
  std::string debugState() const override {
    if (state_ == State::Ready) {
      // "page=3/120 text=..." (the page's own text, so scripts can wait for a word or check accents)
      std::string s = "page=" + std::to_string(cur_ + 1) + "/" + std::to_string(pages_.size()) + " text=";
      if (loadedChapter_ == pages_[cur_].chapter)
        for (const ui::Line& line : view_->page().lines)
          for (const ui::Seg& seg : line.segs) s += seg.text + " ";
      return s;
    }
    return state_ == State::Loading ? "loading" : "error";
  }

  void build(ui::Column& root) override {
    setTitle(file_);
    ctx().store.set("open", file_);
    view_ = root.add<PageView>();
    view_->pageHint = Hint::Page;  // page turns: Fast, no settle sweep, a Quality clean every pageCleanEvery pages
    view_->flex = 1;
    view_->setInsets(kMarginX, kMarginTop, kMarginX, 0);
    view_->setStyle(pageStyle(font_));
    view_->onZone = [this](PageView::Zone z) { onZone(z); };
    footer_ = root.add<PageFooter>();
    footer_->forwardTo = view_;
    view_->setMessage("Preparing book...");
    open();
  }

 private:
  enum class State { Loading, Ready, Error };

  // Shared with the worker job: how far it is, and whether to stop.
  struct Steps {
    std::atomic<int> done{0}, total{1};
    std::atomic<bool> cancel{false};
  };

  void showPreparing() {
    if (state_ != State::Loading) return;
    const int total = std::max(1, steps_->total.load());
    const int pct = std::min(99, steps_->done.load() * 100 / total);
    view_->setMessage(pct > 0 ? "Preparing book... " + std::to_string(pct) + "%" : "Preparing book...");
    if (state_ == State::Loading) ctx().timer(700, [this] { showPreparing(); });
  }

  void open() {
    const std::string file = file_;
    const ui::TextStyle style = pageStyle(font_);
    const uint32_t t0 = ctx().clock.millis();
    steps_ = std::make_shared<Steps>();
    auto steps = steps_;
    ctx().timer(1500, [this] { showPreparing(); });  // a book that opens quickly never shows the percentage
    ctx().async(
        [file, style, steps] {
          // Worker thread: its own metrics canvas (the frame canvas belongs to the UI thread).
          ui::forgetLoadedFont();
          Canvas metrics;
          metrics.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
          metrics.createSprite(16, 16);
          ui::CanvasMetrics m(metrics);
          OpenResult r = openBook(file, style, m, [steps](int done, int total) {
            steps->done = done;
            steps->total = total;
            return !steps->cancel.load();
          });
          ui::forgetLoadedFont();
          return r;
        },
        [this, t0](OpenResult r) {
          const uint32_t ms = ctx().clock.millis() - t0;
          if (!r.ok) {
            state_ = State::Error;
            view_->setMessage(r.error);
            ctx().log("open failed: %s", r.error.c_str());
            return;
          }
          ctx().log("opened '%s': %zu pages, %u ms (%s; convert %u ms, paginate %u ms)", r.meta.title.c_str(), r.pages.size(),
                    static_cast<unsigned>(ms), r.fromCache ? "cached index" : "indexed", static_cast<unsigned>(r.importMs),
                    static_cast<unsigned>(r.indexMs));
          if (!r.fromCache)
            ctx().log("  structure %u, cover %u, meta %u ms",  static_cast<unsigned>(r.timing.structureMs), static_cast<unsigned>(r.timing.coverMs), static_cast<unsigned>(r.timing.metaMs));
          if (!r.fromCache)
            ctx().log("  zip %u, xhtml %u, write %u, read %u, parse %u, layout %u ms (measuring %u ms, cache %u hits / %u misses)", static_cast<unsigned>(r.timing.zipMs),
                      static_cast<unsigned>(r.timing.convertMs), static_cast<unsigned>(r.timing.writeMs), static_cast<unsigned>(r.timing.readMs),
                      static_cast<unsigned>(r.timing.parseMs), static_cast<unsigned>(r.timing.layoutMs), static_cast<unsigned>(r.timing.measureMs),
                      static_cast<unsigned>(r.timing.hits), static_cast<unsigned>(r.timing.misses));
          meta_ = std::move(r.meta);
          pages_ = std::move(r.pages);
          const bool resumed = r.progress.opened > 0;
          cur_ = resumed ? pageIndexFor(pages_, r.progress.chapter, r.progress.pos) : 0;
          state_ = State::Ready;
          showPage(cur_);
          // The cover is a write (slow on the card): after the book is open, and queued after the first page's chapter.
          // Queued before it, the page came 1.9 s after the book was ready and taps were dropped meanwhile (device, 2026-11-16).
          if (meta_.epub && !meta_.cover.empty() && coverFile(meta_).empty()) {
            const BookMeta m = meta_;
            ctx().async([m] { return extractCover(m); }, [](bool) {});
          }
        });
  }

  // Makes page `idx` the current one: loads its chapter first if another one is loaded.
  void showPage(int idx) {
    const PageRef ref = pages_[idx];
    if (ref.chapter == loadedChapter_) {
      cur_ = idx;
      view_->goTo(ref.pos);
      updateFooter();
      schedulePrefetch();
      return;
    }
    if (ref.chapter == nextCh_ && !nextParas_.empty()) {  // loaded ahead while the previous page was read
      enterChapter(idx, ref.chapter, std::move(nextParas_));
      nextCh_ = -1;
      return;
    }
    loading_ = true;
    const BookMeta meta = meta_;
    const int ch = ref.chapter;
    ctx().async(
        [meta, ch] {
          std::string text;
          ChapterLoader loader(meta);
          loader.load(ch, text);
          return ui::parseMarked(text);
        },
        [this, idx, ch](std::vector<ui::Paragraph> paras) {
          loading_ = false;
          enterChapter(idx, ch, std::move(paras));
        });
  }

  void enterChapter(int idx, int ch, std::vector<ui::Paragraph> paras) {
    loadedChapter_ = ch;
    std::vector<ui::TextPos> starts;
    for (const PageRef& p : pages_)
      if (p.chapter == ch) starts.push_back(p.pos);
    cur_ = idx;
    view_->setMessage("");
    view_->setChapter(std::move(paras), std::move(starts), pages_[idx].pos);
    updateFooter();
    schedulePrefetch();
  }

  // The last page of a chapter: load the next chapter on the worker now (reading and converting it from the EPUB took ~140 ms
  // on the tap, DISPLAY.md "Tap latency"); showPage() then uses it at once.
  void loadNextChapter(int ch) {
    if (ch == nextCh_ || nextLoading_ || ch == loadedChapter_) return;
    nextLoading_ = true;
    const BookMeta meta = meta_;
    ctx().async(
        [meta, ch] {
          std::string text;
          ChapterLoader loader(meta);
          loader.load(ch, text);
          return ui::parseMarked(text);
        },
        [this, ch](std::vector<ui::Paragraph> paras) {
          nextLoading_ = false;
          if (ch == loadedChapter_) return;  // the reader got there first and loaded it itself
          nextCh_ = ch;
          nextParas_ = std::move(paras);
        });
  }

  // What the sleep screen shows (ReadApp::sleepInfo reads these): the book being read, its cover and progress.
  void rememberForSleep() {
    Store& st = ctx().store;
    st.set("last.title", meta_.title);
    st.set("last.author", meta_.author);
    st.set("last.percent", percentOf(cur_, pages_.size()));
    st.set("last.cover", coverFile(meta_));
  }

  void updateFooter() {
    rememberForSleep();
    const int ch = std::min<int>(pages_[cur_].chapter, static_cast<int>(meta_.chapters.size()) - 1);
    char right[32];
    std::snprintf(right, sizeof right, "%d / %zu", cur_ + 1, pages_.size());
    const int pct = ctx().os().platform().power.batteryPercent();
    // The contents title of the chapter when the book has one for it, else the book's title (never a piece of a heading).
    footer_->set(ch >= 0 && meta_.chapters[ch].toc ? meta_.chapters[ch].title : meta_.title, right, pct >= 0 && pct < 20 && !ctx().os().platform().power.charging());
  }

  void onZone(PageView::Zone z) {
    if (state_ != State::Ready) {
      if (z == PageView::Zone::Menu) {  // while preparing (or after an error) the centre leaves
        if (steps_) steps_->cancel = true;
        leaveBook();
        ctx().nav.pop();
      }
      return;
    }
    if (loading_) return;
    switch (z) {
      case PageView::Zone::Next:
        if (cur_ + 1 < static_cast<int>(pages_.size())) turnTo(cur_ + 1);
        break;
      case PageView::Zone::Prev:
        if (cur_ > 0) turnTo(cur_ - 1);
        break;
      case PageView::Zone::Menu:
        openMenu();
        break;
    }
  }

  void turnTo(int idx) {
    showPage(idx);
    if (++turns_ % 5 == 0) {
      // Not on the tap: the flash write (~35 ms on the device) would delay the page. A second later, when nobody waits.
      // Leaving the book and power-off save anyway (onStop / prepareForPowerOff paths call saveProgress()).
      ctx().timer(1000, [this] { saveProgress(); });
    }
  }

  // While the page is being read, draw the next one ahead (DISPLAY.md "Tap latency": the next turn copies it in instead
  // of ~90 ms of layout and drawing). 600 ms after a page shows: the turn's Fast waveform is done by then.
  void schedulePrefetch() {
    const int me = cur_;
    ctx().timer(600, [this, me] {
      if (cur_ != me || loading_ || state_ != State::Ready) return;
      const int n = static_cast<int>(pages_.size());
      if (me + 1 >= n) return;
      if (pages_[me + 1].chapter == loadedChapter_) view_->prefetch(pages_[me + 1].pos);
      // The next chapter starts within two pages: load it now. Measured on the device: 129 ms at book open, but ~930 ms while
      // reading (the worker shares core 0 with the panel's waveform task after each turn), so one page is not always enough.
      for (int k = me + 1; k <= me + 2 && k < n; ++k)
        if (pages_[k].chapter != loadedChapter_) {
          loadNextChapter(pages_[k].chapter);
          break;
        }
    });
  }

  // The menu: a sheet over the page (docs/apps/reader.md "Page"). Text size, go to (by 10% and by page), contents, clean screen.
  void openMenu() {
    auto sheet = std::make_unique<ui::Sheet>();
    ui::Column* c = sheet->content();
    ui::Sheet* raw = sheet.get();
    c->add<ui::Label>(meta_.title, Type::Title);
    auto caption = [c](const char* t) { c->add<ui::Label>(t, Type::Caption, ui::secondary); };
    caption("Text size");
    auto* size = c->add<ui::Segmented>(std::vector<std::string>{"S", "M", "L", "XL"}, font_);
    size->onChange = [this, raw](int i) {
      raw->close();
      changeFont(i);
    };
    caption("Go to: percent of the book, or page by page");
    const int total = static_cast<int>(pages_.size());
    auto* level = c->add<ui::LevelBar>(std::max(10, (percentOf(cur_, pages_.size()) + 9) / 10 * 10));
    c->add<SegmentLabels>();
    auto* stepper = c->add<ui::Stepper>(cur_ + 1, 1, std::max(1, total), 1, [total](int v) { return "Page " + std::to_string(v) + " of " + std::to_string(total); });
    level->onChange = [this, stepper](int pct) {
      const int target = std::min(static_cast<int>(pages_.size()) - 1, (static_cast<int>(pages_.size()) - 1) * pct / 100);
      jumpTo(target);
      stepper->setValue(target + 1, Hint::Content);  // cur_ follows once the chapter is loaded
    };
    stepper->onChange = [this](int v) { jumpTo(v - 1); };
    auto* row = c->add<ui::Row>(ui::s1);
    auto* contents = row->add<ui::Button>("Contents", ui::Button::Kind::Secondary);
    contents->flex = 1;
    contents->onClick = [this, raw] {
      raw->close();
      openContents();
    };
    auto* clean = row->add<ui::Button>("Clean", ui::Button::Kind::Secondary);
    clean->flex = 1;
    clean->onClick = [this, raw] {
      raw->close();
      ctx().os().displayManager().forceClean();
    };
    auto* lib = row->add<ui::Button>("Library");
    lib->flex = 1;
    lib->onClick = [this, raw] {
      raw->close();
      leaveBook();
      ctx().nav.pop();
    };
    view_->host()->addOverlay(std::move(sheet));
  }

  void jumpTo(int idx) {
    if (loading_ || idx < 0 || idx >= static_cast<int>(pages_.size()) || idx == cur_) return;
    turnTo(idx);
  }

  // A new text size: the position is saved, the book is paginated again for it (instant when that index is cached).
  void changeFont(int idx) {
    if (idx == font_ || loading_) return;
    saveProgress();
    font_ = idx;
    ctx().store.set("font", idx);
    view_->setStyle(pageStyle(font_));
    loadedChapter_ = -1;
    state_ = State::Loading;
    view_->setMessage("Preparing book...");
    open();
  }

  void openContents() {
    std::vector<std::string> titles;
    std::vector<int> firstPage(meta_.chapters.size(), -1);
    for (int i = 0; i < static_cast<int>(pages_.size()); ++i)
      if (pages_[i].chapter < firstPage.size() && firstPage[pages_[i].chapter] < 0) firstPage[pages_[i].chapter] = i;
    std::vector<ContentsEntry> entries;
    for (size_t i = 0; i < meta_.chapters.size(); ++i)
      if (firstPage[i] >= 0) entries.push_back({meta_.chapters[i].title, firstPage[i]});
    ctx().nav.push<ContentsScreen>(entries, pages_[cur_].chapter < firstPage.size() ? firstPage[pages_[cur_].chapter] : 0, [this](int page) {
      jumpTo(page);
      ctx().nav.pop();
    });
  }

  void saveProgress() {
    if (state_ != State::Ready || pages_.empty()) return;
    Progress p;
    p.chapter = pages_[cur_].chapter;
    p.pos = pages_[cur_].pos;
    p.percent = percentOf(cur_, pages_.size());
    p.opened = ctx().clock.now();
    if (p.opened <= 0) p.opened = 1;  // no RTC: still "opened"
    writeProgress(meta_.id, p);
  }

  std::string file_;
  int font_;
  std::shared_ptr<Steps> steps_;
  State state_ = State::Loading;
  bool loading_ = false;
  PageView* view_ = nullptr;
  PageFooter* footer_ = nullptr;
  BookMeta meta_;
  std::vector<PageRef> pages_;
  int cur_ = 0;
  int loadedChapter_ = -1;
  int nextCh_ = -1;                     // the next chapter, loaded ahead (loadNextChapter)
  std::vector<ui::Paragraph> nextParas_;
  bool nextLoading_ = false;
  int turns_ = 0;
};

class ReadApp : public App {
 public:
  bool sleepInfo(SleepInfo& info) override {
    if (!store_) return false;
    info.title = store_->get("last.title", std::string());
    if (info.title.empty()) return false;
    info.subtitle = store_->get("last.author", std::string());
    info.percent = store_->get("last.percent", -1);
    info.image = store_->get("last.cover", std::string());
    return true;
  }
  void onStart(AppContext& ctx) override {
    store_ = &ctx.store;
    AppContext* c = &ctx;
    // The book that was open at the last power-off comes back on its page (the library stays under it, not scanned yet).
    std::string open = ctx.store.get("open", std::string());
    if (!open.empty() && !fs::exists(std::string(kBooksDir) + "/" + open)) open.clear();
    ctx.nav.push<LibraryScreen>([c](const std::string& file) { c->nav.push<PageScreen>(file, c->store.get("font", 1)); }, !open.empty());
    if (!open.empty()) ctx.nav.push<PageScreen>(open, ctx.store.get("font", 1));
  }

 private:
  Store* store_ = nullptr;
};

POS_REGISTER_APP(ReadApp, {.id = "read", .name = "Read", .icon = Icon::BookOpen, .order = 10});

}  // namespace

}  // namespace pos::reader
