// Hacker News (docs/apps/hackernews.md). Two screens: a list of thirty stories (Top / New / Ask / Show, six a page) and a
// story with its comments as pages of text, replies moved right under what they answer. Read only.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "apps/hackernews/hackernews_model.h"
#include "core/app.h"
#include "core/clipboard.h"
#include "core/fs.h"
#include "core/strings.h"
#include "core/timefmt.h"
#include "ui/qr.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::hackernews {

namespace {

using ui::Type;

constexpr int kRowsPerPage = 6;
constexpr int kRowH = 106;          // two lines of title and the line under it
constexpr int kRankW = 40;          // the column of the story's place in the list
constexpr int64_t kFreshSec = 300;  // a list this young is shown without a read
constexpr int64_t kResumeSec = 3600;  // opened again within the hour: the story that was open, at its page
constexpr const char* kCacheDir = "/data/cache/hackernews/";

// One story of the list: its place, the title on up to two lines, points / comments / age / site under it.
class StoryRow : public ui::Pressable {
 public:
  void set(int rank, const std::string& title, const std::string& meta) {
    if (rank == rank_ && title == title_ && meta == meta_) return;
    rank_ = rank;
    title_ = title;
    meta_ = meta;
    invalidate(Hint::Content);
  }
  ui::Size measure(int availW) override { return {availW, kRowH}; }
  void draw(Canvas& c) override {
    const Color back = pressed_ ? ui::ink : bg(), fg = pressed_ ? ui::paper : ui::ink, sub = pressed_ ? ui::paper : ui::secondary;
    if (pressed_) c.fillRect(frame.x, frame.y, frame.w, frame.h, ui::ink);
    const int left = frame.x + kRankW, textW = frame.w - kRankW;
    ui::Label wrap(title_, Type::BodyStrong);
    wrap.maxLines = 2;
    const std::vector<std::string> lines = wrap.lines(c, textW);
    const int lineH = ui::lineHeight(c, Type::BodyStrong), metaH = ui::lineHeight(c, Type::Caption);
    const int n = static_cast<int>(lines.size());
    int y = frame.y + (frame.h - (n * lineH + 4 + metaH)) / 2;
    ui::drawText(c, std::to_string(rank_), left - ui::s1 - 2, y, Type::Body, sub, ui::Anchor::TopRight, back);
    for (const std::string& line : lines) {
      ui::drawText(c, line, left, y, Type::BodyStrong, fg, ui::Anchor::TopLeft, back);
      y += lineH;
    }
    ui::drawText(c, ui::ellipsize(c, meta_, Type::Caption, textW), left, y + 4, Type::Caption, sub, ui::Anchor::TopLeft, back);
    c.drawFastHLine(frame.x, frame.y + frame.h - 1, frame.w, pressed_ ? ui::ink : ui::divider);
  }

 private:
  int rank_ = 0;
  std::string title_, meta_;
};

// A line across: between the story's title and its comments.
class Rule : public ui::Widget {
 public:
  ui::Size measure(int availW) override { return {availW, 1}; }
  void draw(Canvas& c) override { c.drawFastHLine(frame.x, frame.y, frame.w, ui::divider); }
};

// The picture over a reason: no Wi-Fi, or anything else (the service answered with an error).
Icon errorIcon(const std::string& why) { return startsWith(why, "No Wi-Fi") ? Icon::WifiOff : Icon::TriangleAlert; }

void setEnabled(ui::Pressable* w, bool on) {
  if (w->enabled == on) return;
  w->enabled = on;
  w->invalidate(Hint::Content);
}

ui::TextStyle threadStyle() {
  ui::TextStyle s;
  s.body = Type::Body;  // the interface's own font: a page holds half as much again as in the reader's
  s.lineSpacing = 1.3f;
  s.indent = 0;        // a comment's paragraphs start at its margin
  s.paragraphGap = 8;
  return s;
}

// What the worker hands back for a story: the story as read just now and its text and comments as paragraphs.
struct Loaded {
  bool ok = false;
  std::string error;
  Story story;
  int comments = 0;                  // comments in the text
  std::shared_ptr<ThreadText> text;
};

// A story: its title, then its own text and the comments, a page at a time. A tap on the right or a swipe turns the page,
// "Next thread" skips the replies to the next answer to the story, "Link" shows the address as a QR code.
class ThreadScreen : public Screen {
 public:
  // `at`: where to open it (the story that was open when the app was left).
  explicit ThreadScreen(Story story, Anchor at = {}) : story_(std::move(story)), at_(at) {}

  void build(ui::Column& root) override {
    setTitle("Hacker News");
    setAction(Icon::RefreshCw, [this] { load(); });
    root.add<ui::Spacer>(ui::s1);
    title_ = root.add<ui::Label>(story_.title, Type::Title);
    title_->maxLines = 3;
    meta_ = root.add<ui::Label>(storyLine(story_, ctx().clock.now()), Type::Caption, ui::secondary);
    root.add<ui::Spacer>(ui::s1);
    root.add<Rule>();
    root.add<ui::Spacer>(ui::s1);
    holder_ = root.add<ui::Stack>();
    holder_->flex = 1;
    block_ = holder_->add<ui::TextBlock>();
    block_->setStyle(threadStyle());
    block_->pageHint = Hint::Page;
    block_->onPageChange = [this](int) { showPage(); };
    block_->onLayout = [this] { showPage(); };
    message_ = holder_->add<ui::Column>(0);
    root.add<ui::Spacer>(ui::s1);
    auto* bottom = root.add<ui::Row>(ui::s1);
    link_ = bottom->add<ui::Button>("Link", [this] { openLink(); }, ui::Button::Kind::Secondary);
    pageLabel_ = bottom->add<ui::Label>("", Type::Caption, ui::secondary);
    pageLabel_->align = ui::TextAlign::Center;
    pageLabel_->flex = 1;
    next_ = bottom->add<ui::Button>("Next thread", [this] { nextThread(); }, ui::Button::Kind::Secondary);
    next_->enabled = false;
    root.add<ui::Spacer>(ui::s1);
    showMessage(Icon::MessageCircle, "Loading comments...");
  }

  void onShow() override {
    if (!text_ && !busy_) load();
  }

  // Kept for the next time the app opens: the story and the place in it. (Back to the list drops it again: the list's
  // onShow. The idle return, the launcher and power-off leave it.)
  void onHide() override {
    if (reading()) at_ = anchorOf(*text_, block_->position());
    ctx().store.set("open", packList(ctx().clock.now(), {story_}));
    ctx().store.set("at", joinInts({static_cast<int>(at_.comment), at_.para, at_.byte}));
  }

  std::string debugState() const override {
    return "story=" + std::to_string(story_.id) + "|comments=" + std::to_string(comments_) + "|" + pageLabel_->text() +
           (busy_ ? "|busy" : "") + (error_.empty() ? "" : "|error=" + error_) + (next_->enabled ? "|next" : "");
  }

 private:
  // Something said in the place of the comments (while there are none to show).
  void showMessage(Icon icon, const std::string& title, const std::string& hint = "", const std::string& button = "",
                   std::function<void()> onButton = {}) {
    message_->removeAll();
    message_->add<ui::EmptyState>(icon, title, hint, button, std::move(onButton))->flex = 1;
    message_->visible = true;
    if (message_->frame.w > 0) message_->layout();
    holder_->invalidate(Hint::Content);
  }

  void clearMessage() {
    if (!message_->visible) return;
    message_->removeAll();
    message_->visible = false;  // it lies over the text: touches go to the text again
    holder_->invalidate(Hint::Content);
  }

  bool reading() const { return text_ && !message_->visible; }

  void load() {
    if (busy_) return;
    if (reading()) at_ = anchorOf(*text_, block_->position());  // read again: the place stays
    busy_ = true;
    error_.clear();
    const unsigned seq = ++seq_;
    const auto send = ctx().transport();
    const Story known = story_;
    const int64_t now = ctx().clock.now();
    ctx().async(
        [send, known, now] {
          Loaded out;
          ThreadResult r = fetchThread(send, known);
          out.ok = r.ok;
          out.error = std::move(r.error);
          out.story = std::move(r.story);
          if (out.ok) {
            out.text = std::make_shared<ThreadText>(threadText(out.story, r.thread, now));
            out.comments = static_cast<int>(r.thread.comments.size());
          }
          return out;
        },
        [this, seq](Loaded r) {
          if (seq != seq_) return;
          if (!r.ok) {
            busy_ = false;
            failed(r.error);
            return;
          }
          ctx().log("story %u: %d comments, %u paragraphs", static_cast<unsigned>(r.story.id), r.comments,
                    static_cast<unsigned>(r.text->paras.size()));
          story_ = std::move(r.story);
          comments_ = r.comments;
          showStory();
          paginate(std::move(r.text), seq);
        });
  }

  // The title and the line under it as read just now (the points and the title can have changed since the list).
  void showStory() {
    meta_->setText(storyLine(story_, ctx().clock.now()));
    if (title_->text() == story_.title) return;
    title_->setText(story_.title);
    if (title_->host()) title_->host()->relayout();  // the title may take another number of lines
  }

  // The pages of a long thread take a second or more to find: on the worker, with the page size the screen has now.
  void paginate(std::shared_ptr<ThreadText> text, unsigned seq) {
    if (text->paras.empty()) {
      busy_ = false;
      text_ = std::move(text);
      block_->setChapter({}, {}, ui::TextPos{});
      showMessage(Icon::MessageCircle, "No comments yet");
      showPage();
      return;
    }
    const ui::TextStyle style = block_->style();
    ctx().async(
        [text, style] {
          // Worker thread: its own metrics canvas (the frame canvas belongs to the UI thread), as the reader does.
          ui::forgetLoadedFont();
          Canvas metrics;
          metrics.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
          metrics.createSprite(16, 16);
          ui::CanvasMetrics m(metrics);
          std::vector<ui::TextPos> starts = ui::paginate(text->paras, style, m);
          ui::forgetLoadedFont();
          return starts;
        },
        [this, text, style, seq](std::vector<ui::TextPos> starts) {
          if (seq != seq_) return;
          busy_ = false;
          text_ = text;
          clearMessage();
          const ui::TextStyle now = block_->style();
          const ui::TextPos pos = posOf(*text_, at_);  // the start, or the place the story was left at
          at_ = Anchor{};
          if (now.width == style.width && now.height == style.height) {
            block_->setChapter(std::move(text_->paras), std::move(starts), pos);
          } else {
            block_->setParagraphs(std::move(text_->paras));  // the page size changed meanwhile: the block finds its pages itself
            block_->goTo(pos);
          }
          showPage();
        });
  }

  // A read failed: said in the place of the comments, or in a toast when comments are on screen.
  void failed(const std::string& why) {
    ctx().log("story %u not read: %s", static_cast<unsigned>(story_.id), why.c_str());
    if (reading()) {
      ctx().toast(why);
      return;
    }
    error_ = why;
    showMessage(errorIcon(why), "Comments not loaded", why, "Retry", [this] { load(); });
  }

  void showPage() {
    const bool has = reading();
    const int n = has ? block_->pageCount() : 0;
    pageLabel_->setText(n > 1 ? "Page " + std::to_string(block_->pageIndex() + 1) + " of " + std::to_string(n) : "");
    setEnabled(next_, has && nextStart() >= 0);
  }

  // The paragraph of the first answer to the story that starts after the page on screen, -1 if there is none.
  int nextStart() {
    const ui::Page& page = block_->page();
    if (page.end) return -1;
    return nextTopLevel(*text_, page.next.byte > 0 ? page.next.para : page.next.para - 1);
  }

  void nextThread() {
    if (!reading()) return;
    const int para = nextStart();
    if (para < 0) return;
    block_->goTo(ui::TextPos{para, 0});
    showPage();
  }

  // The story's link as a QR code for the phone; a link too long for the code gives the story's page on the site.
  void openLink() {
    ui::openSheet(*link_, [this](ui::Sheet& s) {
      std::string url = story_.url.empty() ? siteUrl(story_.id) : story_.url;
      auto qr = std::make_unique<ui::QrCode>(url, 300);
      if (!qr->ok()) {
        url = siteUrl(story_.id);
        qr = std::make_unique<ui::QrCode>(url, 300);
      }
      s.content()->add<ui::Label>("Open on your phone", Type::Title);
      s.content()->add<ui::Column>(0, ui::Align::Center)->adopt(std::move(qr));
      auto* address = s.content()->add<ui::Label>(url, Type::Caption, ui::secondary);
      address->align = ui::TextAlign::Center;
      address->maxLines = 2;
      auto* row = s.content()->add<ui::Row>(ui::s1);
      auto* copy = row->add<ui::Button>("Copy link", s.closing([this, url] {
        clipboard::set(url, ctx().appId());
        ctx().toast(clipboard::syncing() ? "Link copied, on its way to the phone" : "Link copied");
      }), ui::Button::Kind::Secondary);
      copy->flex = 1;
      auto* close = row->add<ui::Button>("Close", s.closing());
      close->flex = 1;
    });
  }

  Story story_;
  Anchor at_;                         // where to open the text once it is read (a story opened again)
  std::shared_ptr<ThreadText> text_;  // null until a read ended; its paragraphs are in the block
  int comments_ = 0;
  std::string error_;
  bool busy_ = false;
  unsigned seq_ = 0;
  ui::Label* title_ = nullptr;
  ui::Label* meta_ = nullptr;
  ui::Stack* holder_ = nullptr;
  ui::TextBlock* block_ = nullptr;
  ui::Column* message_ = nullptr;
  ui::Label* pageLabel_ = nullptr;
  ui::Button* link_ = nullptr;
  ui::Button* next_ = nullptr;
};

// The lists. The saved copy of the chosen list is on screen in the first frame; a read follows unless the copy is young.
class ListScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Hacker News");
    setAction(Icon::RefreshCw, [this] { refresh(); });
    if (!sectionFromKey(ctx().store.get("section", "top"), section_)) section_ = Section::Top;
    root.add<ui::Spacer>(ui::s1);
    std::vector<std::string> names;
    for (int i = 0; i < kSections; ++i) names.push_back(sectionName(static_cast<Section>(i)));
    tabs_ = root.add<ui::Segmented>(names, static_cast<int>(section_));
    tabs_->onChange = [this](int i) { choose(static_cast<Section>(i)); };
    root.add<ui::Spacer>(ui::s1);
    holder_ = root.add<ui::Column>(0);
    holder_->flex = 1;
    status_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    root.add<ui::Spacer>(ui::s1);
    loadSaved();
    fill();
    // The story that was open when the app was left less than an hour ago opens again, at its page.
    int64_t left = 0;
    std::vector<Story> open;
    const int64_t now = ctx().clock.now();
    if (unpackList(ctx().store.get("open", ""), left, open) && open.size() == 1 && now >= left && now - left < kResumeSec) {
      const std::vector<int> at = splitInts(ctx().store.get("at", ""));
      if (at.size() == 3) resumeAt_ = Anchor{static_cast<uint32_t>(at[0]), at[1], at[2]};
      resume_ = std::move(open);
    }
  }

  void onShow() override {
    shown_ = true;
    if (!resume_.empty()) {
      const Story story = resume_[0];
      resume_.clear();
      ctx().nav.push<ThreadScreen>(story, resumeAt_);  // (the list is read when the user comes back to it)
      return;
    }
    ctx().store.set("open", "");  // no story is open
    if (stale_) fill();  // an answer came while a story was open
    if (!busy_ && !fresh()) refresh();
    else showRows();     // the ages move on
  }

  void onHide() override { shown_ = false; }

  std::string debugState() const override {
    return std::string("section=") + sectionKey(section_) + "|stories=" + std::to_string(stories_.size()) +
           "|page=" + std::to_string(pager_ ? pager_->page() + 1 : 0) + "/" + std::to_string(pager_ ? pager_->pages() : 0) + "|" +
           status_->text() + (busy_ ? "|busy" : "") + (error_.empty() ? "" : "|error=" + error_);
  }

 private:
  bool fresh() const {
    const int64_t now = ctx().clock.now();
    return fetched_ > 0 && now >= fetched_ && now - fetched_ < kFreshSec;
  }

  void loadSaved() {
    stories_.clear();
    fetched_ = 0;
    if (!unpackList(ctx().cache.read(sectionKey(section_)), fetched_, stories_)) fetched_ = 0;
  }

  // Another list: its saved copy at once, a read if that is old.
  void choose(Section s) {
    if (s == section_) return;
    section_ = s;
    ctx().store.set("section", sectionKey(s));
    ++seq_;  // an answer on its way is for the list that was left
    busy_ = false;
    error_.clear();
    loadSaved();
    if (pager_) pager_->setPage(0);
    fill();
    if (!fresh()) refresh();
  }

  // The rows, or what is said while there are none.
  void fill() {
    stale_ = false;
    const bool list = !stories_.empty();
    if (list != static_cast<bool>(pager_) || !list) {
      holder_->removeAll();
      pager_ = nullptr;
      if (list) {
        pager_ = holder_->add<ui::Pager>();
        pager_->flex = 1;
        auto* rows = pager_->add<ui::Column>(0);
        for (int i = 0; i < kRowsPerPage; ++i) {
          rows_[i] = rows->add<StoryRow>();
          rows_[i]->onClick = [this, i] { open(pager_->page() * kRowsPerPage + i); };
        }
        pager_->onPage = [this](int) { showRows(); };
      } else if (!error_.empty()) {
        holder_->add<ui::EmptyState>(errorIcon(error_), "Hacker News not loaded", error_, "Retry", [this] { refresh(); })->flex = 1;
      } else if (busy_ || fetched_ == 0) {
        holder_->add<ui::EmptyState>(Icon::Hackernews, "Loading...")->flex = 1;
      } else {
        holder_->add<ui::EmptyState>(Icon::Hackernews, "No stories")->flex = 1;
      }
      if (holder_->host()) holder_->host()->relayout();
    }
    if (pager_) {
      const int pages = (static_cast<int>(stories_.size()) + kRowsPerPage - 1) / kRowsPerPage;
      pager_->setPages(pages);
      const bool bar = pages > 1;
      if (pager_->bar()->visible != bar) {
        pager_->bar()->visible = bar;
        if (pager_->frame.w > 0) pager_->layout();
        pager_->invalidate(Hint::Content);
      }
      showRows();
    }
    showStatus();
  }

  void showRows() {
    if (!pager_) return;
    const int64_t now = ctx().clock.now();
    const int first = pager_->page() * kRowsPerPage, count = static_cast<int>(stories_.size());
    bool cameOrWent = false;
    for (int i = 0; i < kRowsPerPage; ++i) {
      const int index = first + i;
      const bool show = index < count;
      cameOrWent = cameOrWent || rows_[i]->visible != show;
      rows_[i]->visible = show;
      if (show) rows_[i]->set(index + 1, stories_[index].title, metaLine(stories_[index], now));
    }
    ui::Widget* list = rows_[0]->parent();
    if (cameOrWent && list->frame.w > 0) {
      list->layout();
      list->invalidate(Hint::Content);  // a row that went leaves no picture behind
    }
  }

  void showStatus() {
    const std::string when = clockText(fetched_);
    if (busy_) {
      status_->setText("Loading...");
    } else if (stories_.empty()) {
      status_->setText("");
    } else if (!error_.empty()) {
      status_->setText("Saved" + (when.empty() ? std::string(" copy") : " " + when) + ": " + error_);
    } else {
      status_->setText(when.empty() ? "Hacker News" : "Updated " + when);
    }
  }

  void refresh() {
    const unsigned seq = ++seq_;
    busy_ = true;
    if (stories_.empty()) {
      error_.clear();
      fill();  // "Loading..." in the place of a Retry
    } else {
      showStatus();
    }
    const auto send = ctx().transport();
    const Section section = section_;
    ctx().async([send, section] { return fetchList(send, section); }, [this, seq, section](ListResult r) {
      if (seq != seq_) return;  // another list was chosen meanwhile
      busy_ = false;
      if (r.ok) {
        const int64_t now = ctx().clock.now();
        ctx().log("%s: %u stories", sectionKey(section), static_cast<unsigned>(r.stories.size()));
        stories_ = std::move(r.stories);
        fetched_ = now;
        error_.clear();
        const std::string name = std::string(kCacheDir) + sectionKey(section);
        ctx().background([name, packed = packList(now, stories_)] { fs::writeFile(name, packed); });
      } else {
        ctx().log("%s not read: %s", sectionKey(section), r.error.c_str());
        error_ = r.error;
      }
      if (shown_) fill();
      else stale_ = true;
    });
  }

  void open(int index) {
    if (index < 0 || index >= static_cast<int>(stories_.size())) return;
    ctx().nav.push<ThreadScreen>(stories_[static_cast<size_t>(index)]);
  }

  Section section_ = Section::Top;
  std::vector<Story> resume_;  // the story to open again (one or none), until the first onShow
  Anchor resumeAt_;
  std::vector<Story> stories_;
  int64_t fetched_ = 0;  // when the list on screen was read
  std::string error_;    // why the last read failed, "" if it did not
  bool busy_ = false, shown_ = false, stale_ = false;
  unsigned seq_ = 0;
  ui::Segmented* tabs_ = nullptr;
  ui::Column* holder_ = nullptr;
  ui::Pager* pager_ = nullptr;
  StoryRow* rows_[kRowsPerPage] = {};
  ui::Label* status_ = nullptr;
};

class HackerNewsApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<ListScreen>(); }
};

}  // namespace

POS_REGISTER_APP(HackerNewsApp, {
    .id = "hackernews",
    .name = "Hacker News",
    .icon = Icon::Hackernews,
    .order = 100,  // after Teletekst
});

}  // namespace pos::hackernews
