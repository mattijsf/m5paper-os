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
      ui::drawText(c, message_, frame.x + frame.w / 2, frame.y + frame.h / 2, Type::Body, ui::secondary, ui::Anchor::MiddleCenter, bg());
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
    invalidate(Hint::Content);
  }
  ui::Size measure(int availW) override { return {availW, kFooterH}; }
  bool onTouch(const Gesture& g) override { return forwardTo ? forwardTo->onTouch(g) : false; }
  void draw(Canvas& c) override {
    const int cy = frame.y + kFooterH / 2 - 4;
    int right = frame.x + frame.w - kMarginX;
    if (low_) {
      ui::drawIcon(c, Icon::BatteryLow, right - 16, cy, 32, ui::secondary, bg());
      right -= 44;
    }
    ui::drawText(c, right_, right, cy, Type::Caption, ui::secondary, ui::Anchor::MiddleRight, bg());
    const int rw = ui::measureText(c, right_, Type::Caption);
    const int room = right - rw - 24 - (frame.x + kMarginX);
    if (room > 40) ui::drawText(c, ui::ellipsize(c, left_, Type::Caption, room), frame.x + kMarginX, cy, Type::Caption, ui::secondary, ui::Anchor::MiddleLeft, bg());
  }

 private:
  std::string left_, right_;
  bool low_ = false;
};

class PageScreen : public Screen {
 public:
  PageScreen(std::string file, int fontIndex) : file_(std::move(file)), font_(fontIndex) {}
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
    view_ = root.add<PageView>();
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
          if (meta_.epub && !meta_.cover.empty() && coverFile(meta_).empty()) {  // the cover is a write: after the book is open
            const BookMeta m = meta_;
            ctx().async([m] { return extractCover(m); }, [](bool) {});
          }
          const bool resumed = r.progress.opened > 0;
          cur_ = resumed ? pageIndexFor(pages_, r.progress.chapter, r.progress.pos) : 0;
          state_ = State::Ready;
          showPage(cur_);
        });
  }

  // Makes page `idx` the current one: loads its chapter first if another one is loaded.
  void showPage(int idx) {
    const PageRef ref = pages_[idx];
    if (ref.chapter == loadedChapter_) {
      cur_ = idx;
      view_->goTo(ref.pos);
      updateFooter();
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
          loadedChapter_ = ch;
          std::vector<ui::TextPos> starts;
          for (const PageRef& p : pages_)
            if (p.chapter == ch) starts.push_back(p.pos);
          cur_ = idx;
          view_->setMessage("");
          view_->setChapter(std::move(paras), std::move(starts), pages_[idx].pos);
          updateFooter();
        });
  }

  void updateFooter() {
    const int ch = std::min<int>(pages_[cur_].chapter, static_cast<int>(meta_.chapters.size()) - 1);
    char right[32];
    std::snprintf(right, sizeof right, "%d / %zu", cur_ + 1, pages_.size());
    const int pct = ctx().os().platform().power.batteryPercent();
    footer_->set(ch >= 0 ? meta_.chapters[ch].title : "", right, pct >= 0 && pct < 20 && !ctx().os().platform().power.charging());
  }

  void onZone(PageView::Zone z) {
    if (state_ != State::Ready) {
      if (z == PageView::Zone::Menu) {  // while preparing (or after an error) the centre leaves
        if (steps_) steps_->cancel = true;
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
    if (++turns_ % 5 == 0) saveProgress();
  }

  void openMenu() {
    auto sheet = std::make_unique<ui::Sheet>();
    ui::Column* c = sheet->content();
    c->add<ui::Label>(meta_.title, Type::Title);
    if (!meta_.author.empty()) c->add<ui::Label>(meta_.author, Type::Body, ui::secondary);
    ui::Sheet* raw = sheet.get();
    c->add<ui::Button>("Library", [this, raw] {
      raw->close();
      ctx().nav.pop();
    });
    c->add<ui::Button>("Close", [raw] { raw->close(); }, ui::Button::Kind::Secondary);
    view_->host()->addOverlay(std::move(sheet));
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
  int turns_ = 0;
};

class ReadApp : public App {
 public:
  void onStart(AppContext& ctx) override {
    AppContext* c = &ctx;
    ctx.nav.push<LibraryScreen>([c](const std::string& file) { c->nav.push<PageScreen>(file, c->store.get("font", 1)); });
  }
};

POS_REGISTER_APP(ReadApp, {.id = "read", .name = "Read", .icon = Icon::BookOpen, .order = 10});

}  // namespace

}  // namespace pos::reader
