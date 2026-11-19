// Hacker News (docs/apps/hackernews.md). Three screens: a list of thirty stories (Top / New / Ask / Show, six a page), a
// story with its comments as pages of text, replies moved right under what they answer, and the article the story links
// to as text from a reader service. Read only.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "apps/hackernews/hackernews_model.h"
#include "core/app.h"
#include "core/clipboard.h"
#include "core/fs.h"
#include "core/strings.h"
#include "core/text_clean.h"
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
constexpr int64_t kFreshSec = 900;  // a list this young is shown without a read (a read moves the rows under the reader's eyes)
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

ui::TextStyle articleStyle() {
  ui::TextStyle s;
  s.body = Type::Body;  // as the comments: the interface's font, with a real bold and headings of its own
  s.indent = 0;
  s.paragraphGap = 12;
  return s;
}

// The story's link as a QR code for the phone; a story without a link, and a link too long for the code, give the
// story's page on the site.
void openLinkSheet(AppContext& ctx, ui::Widget& from, const Story& story) {
  AppContext* c = &ctx;
  ui::openSheet(from, [c, &story](ui::Sheet& s) {
    std::string url = story.url.empty() ? siteUrl(story.id) : story.url;
    auto qr = std::make_unique<ui::QrCode>(url, 300);
    if (!qr->ok()) {
      url = siteUrl(story.id);
      qr = std::make_unique<ui::QrCode>(url, 300);
    }
    s.content()->add<ui::Label>("Open on your phone", Type::Title);
    s.content()->add<ui::Column>(0, ui::Align::Center)->adopt(std::move(qr));
    auto* address = s.content()->add<ui::Label>(url, Type::Caption, ui::secondary);
    address->align = ui::TextAlign::Center;
    address->maxLines = 2;
    auto* row = s.content()->add<ui::Row>(ui::s1);
    auto* copy = row->add<ui::Button>("Copy link", s.closing([c, url] {
      clipboard::set(url, c->appId());
      c->toast(clipboard::syncing() ? "Link copied, on its way to the phone" : "Link copied");
    }), ui::Button::Kind::Secondary);
    copy->flex = 1;
    auto* close = row->add<ui::Button>("Close", s.closing());
    close->flex = 1;
  });
}

// A screen whose middle is pages of text (a ui::TextBlock: a tap on the right or a swipe turns the page), with a message
// in its place while there is no text: loading, nothing to show, why a read failed.
class TextScreen : public Screen {
 protected:
  // The text's place in the screen: call from build().
  void addText(ui::Column& root, const ui::TextStyle& style) {
    holder_ = root.add<ui::Stack>();
    holder_->flex = 1;
    block_ = holder_->add<ui::TextBlock>();
    block_->setStyle(style);
    block_->pageHint = Hint::Page;
    block_->onPageChange = [this](int) { showPage(); };
    block_->onLayout = [this] { showPage(); };
    message_ = holder_->add<ui::Column>(0);
    message_->visible = false;
  }

  void showMessage(Icon icon, const std::string& title, const std::string& hint = "", const std::string& button = "",
                   std::function<void()> onButton = {}) {
    message_->removeAll();
    message_->add<ui::EmptyState>(icon, title, hint, button, std::move(onButton))->flex = 1;
    message_->visible = true;
    if (message_->frame.w > 0) message_->layout();
    holder_->invalidate(Hint::Content);
  }

  // "Loading ...": a line of ink and nothing gray, so the Fast pass of the new screen already shows it as it is (an icon
  // in gray made the settle pass 0.6 s later a visible second change: camera, 2026-11-18).
  void showLoading(const std::string& text) {
    message_->removeAll();
    message_->add<ui::Spacer>(0, 1);
    auto* line = message_->add<ui::Label>(text, Type::Title);
    line->align = ui::TextAlign::Center;
    message_->add<ui::Spacer>(0, 1);
    message_->visible = true;
    if (message_->frame.w > 0) message_->layout();
    holder_->invalidate(Hint::Content);
  }

  void clearMessage() {
    if (!message_->visible) return;
    message_->removeAll();
    message_->visible = false;  // it lies over the text: touches go to the text again
    holder_->invalidate(Hint::Page);  // the text comes as a page does: one Fast pass, no settle sweep over it
  }

  // Text is on screen (not a message in its place).
  bool reading() const { return hasText_ && !message_->visible; }

  // "Page 3 of 57", "" for a single page, no text, or while the pages are still being counted.
  std::string pageText() {
    const int n = reading() && !counting_ ? block_->pageCount() : 0;
    return n > 1 ? "Page " + std::to_string(block_->pageIndex() + 1) + " of " + std::to_string(n) : std::string();
  }

  // Shows `paras` at the page of `pos`. Where the pages of a long text start takes a second or more to find (700
  // paragraphs: 1.5 s on the device): on the worker, with the page size the screen has now. A text shown from its start
  // does not wait for that: its first page needs no page starts, so it is on screen at once and the page count follows
  // (pages turn once it is there). Dropped if seq_ moved on meanwhile.
  void showText(std::vector<ui::Paragraph> paras, ui::TextPos pos) {
    const unsigned seq = seq_;
    auto text = std::make_shared<std::vector<ui::Paragraph>>(std::move(paras));
    const ui::TextStyle style = block_->style();
    const bool fromStart = pos == ui::TextPos{};
    if (fromStart) {
      hasText_ = true;
      counting_ = true;
      clearMessage();
      block_->setChapter(*text, {ui::TextPos{}}, ui::TextPos{});  // (a copy: the worker reads the other one)
      showPage();
    }
    ctx().async(
        [text, style] {
          // Worker thread: its own metrics canvas (the frame canvas belongs to the UI thread), as the reader does.
          ui::forgetLoadedFont();
          Canvas metrics;
          metrics.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
          metrics.createSprite(16, 16);
          ui::CanvasMetrics m(metrics);
          std::vector<ui::TextPos> starts = ui::paginate(*text, style, m);
          ui::forgetLoadedFont();
          return starts;
        },
        [this, text, style, seq, pos, fromStart](std::vector<ui::TextPos> starts) {
          if (seq != seq_) return;
          busy_ = false;
          counting_ = false;
          const ui::TextStyle now = block_->style();
          const bool same = now.width == style.width && now.height == style.height;
          if (fromStart) {
            if (same) block_->setPageStarts(std::move(starts));  // the page on screen stays as it is
            // (else the page size changed meanwhile and the block finds its pages itself)
          } else {
            hasText_ = true;
            clearMessage();
            if (same) {
              block_->setChapter(std::move(*text), std::move(starts), pos);
            } else {
              block_->setParagraphs(std::move(*text));
              block_->goTo(pos);
            }
          }
          showPage();
        });
  }

  // The page changed, text came or went: the page label, the buttons.
  virtual void showPage() = 0;

  ui::Stack* holder_ = nullptr;
  ui::TextBlock* block_ = nullptr;
  ui::Column* message_ = nullptr;
  bool hasText_ = false;  // a read ended and its text is in the block
  bool counting_ = false; // the first page is on screen, where the others start is still being found
  bool busy_ = false;
  unsigned seq_ = 0;      // a newer read makes the answers of older ones void
  std::string error_;     // why the last read failed, "" if it did not
};

// Where to open a story again (docs/apps/hackernews.md "Opened again"): which screens were open over the list, in order
// ('a' the article, 't' the comments: "a", "at", "t", "ta"), and the place in each.
struct Resume {
  std::string chain;  // the screens still to open after this one
  Anchor at;          // in the comments
  ui::TextPos articleAt;
};

class ThreadScreen;
void pushThread(AppContext& ctx, const Story& story, bool overArticle, Resume resume);

// "141 comments": what the article's button says.
std::string commentsLabel(int n) { return n == 1 ? std::string("1 comment") : std::to_string(std::max(0, n)) + " comments"; }

// The page a story links to, as text from the reader service (docs/apps/hackernews.md "The article"). What a tap on a story
// with a link opens; its comments are a button away.
class ArticleScreen : public TextScreen {
 public:
  // `overThread`: opened from the comments (Comments then goes back to them). `resume`: where to open it, and what was
  // open over it, when the app was left with it open.
  ArticleScreen(Story story, bool overThread, Resume resume = {})
      : story_(std::move(story)), overThread_(overThread), at_(resume.articleAt), resume_(std::move(resume)) {}

  void build(ui::Column& root) override {
    const std::string site = hostOfUrl(story_.url);
    setTitle(site.empty() ? std::string("Article") : site);
    setAction(Icon::RefreshCw, [this] { load(); });
    root.add<ui::Spacer>(ui::s1);
    addText(root, articleStyle());
    root.add<ui::Spacer>(ui::s1);
    auto* bottom = root.add<ui::Row>(ui::s1);
    link_ = bottom->add<ui::Button>("Link", [this] { openLinkSheet(ctx(), *link_, story_); }, ui::Button::Kind::Secondary);
    pageLabel_ = bottom->add<ui::Label>("", Type::Caption);  // ink: it changes with the page, in the page's Fast pass, and is not settled
    pageLabel_->align = ui::TextAlign::Center;
    pageLabel_->flex = 1;
    comments_ = bottom->add<ui::Button>(commentsLabel(story_.comments), [this] { openComments(); }, ui::Button::Kind::Secondary);
    root.add<ui::Spacer>(ui::s1);
    showLoading("Loading the article...");
  }

  void onShow() override {
    ctx().store.set("stack", overThread_ ? "ta" : "a");
    if (!resume_.chain.empty()) {  // the comments were open over it when the app was left: straight to them
      Resume next = resume_;
      next.chain.erase(0, 1);
      resume_.chain.clear();
      pushThread(ctx(), story_, true, next);  // (the article is read when the user comes back to it)
      return;
    }
    if (!hasText_ && !busy_) load();
  }

  // Kept for the next time the app opens: the place in the article.
  void onHide() override {
    if (reading()) at_ = block_->position();
    ctx().store.set("open", packList(ctx().clock.now(), {story_}));
    ctx().store.set("article", joinInts({at_.para, at_.byte}));
  }

  std::string debugState() const override {
    return "article=" + std::to_string(story_.id) + "|" + pageLabel_->text() + (busy_ ? "|busy" : "") +
           (error_.empty() ? "" : "|error=" + error_);
  }

 private:
  void load() {
    if (busy_) return;
    if (reading()) at_ = block_->position();  // read again: the place stays (the text is the same one)
    busy_ = true;
    error_.clear();
    const unsigned seq = ++seq_;
    const auto send = ctx().transport();
    const std::string url = story_.url;
    ctx().async(
        [send, url] {
          ArticleResult r = fetchArticle(send, url);
          auto paras = std::make_shared<std::vector<ui::Paragraph>>();
          if (r.ok) *paras = ui::parseMarked(r.article.text);
          return std::make_pair(std::move(r.error), paras);
        },
        [this, seq](std::pair<std::string, std::shared_ptr<std::vector<ui::Paragraph>>> r) {
          if (seq != seq_) return;
          if (r.second->empty()) {
            busy_ = false;
            ctx().log("article of %u not read: %s", static_cast<unsigned>(story_.id), r.first.c_str());
            if (reading()) {
              ctx().toast(r.first);
              return;
            }
            error_ = r.first;
            showMessage(errorIcon(error_), "Article not loaded", error_, "Retry", [this] { load(); });
            return;
          }
          ctx().log("article of %u: %u paragraphs", static_cast<unsigned>(story_.id), static_cast<unsigned>(r.second->size()));
          const int paras = static_cast<int>(r.second->size());
          const ui::TextPos pos = at_.para < paras ? at_ : ui::TextPos{};
          at_ = ui::TextPos{};
          showText(std::move(*r.second), pos);
        });
  }

  void showPage() override { pageLabel_->setText(pageText(), Hint::Page); }  // with the page: one pass, no settle after it

  // The comments: back to them when the article was opened from them, else opened over the article.
  void openComments() {
    if (overThread_) ctx().nav.pop();
    else pushThread(ctx(), story_, true, Resume{});
  }

  Story story_;
  bool overThread_;
  ui::TextPos at_;  // where to open the text once it is read
  Resume resume_;
  ui::Label* pageLabel_ = nullptr;
  ui::Button* link_ = nullptr;
  ui::Button* comments_ = nullptr;
};

// What the worker hands back for a story: the story as read just now and its text and comments as paragraphs.
struct Loaded {
  bool ok = false;
  std::string error;
  Story story;
  int comments = 0;                  // comments in the text
  std::shared_ptr<ThreadText> text;
};


// A story: its title, then its own text and the comments, a page at a time. "Next thread" skips the replies to the next
// answer to the story; "Article" opens the page the story links to as text ("Link", its address as a QR code, for a
// story without one).
class ThreadScreen : public TextScreen {
 public:
  // `overArticle`: opened from the article (Article then goes back to it). `resume`: as for the article.
  ThreadScreen(Story story, bool overArticle, Resume resume = {})
      : story_(std::move(story)), overArticle_(overArticle), at_(resume.at), resume_(std::move(resume)) {}

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
    addText(root, threadStyle());
    root.add<ui::Spacer>(ui::s1);
    auto* bottom = root.add<ui::Row>(ui::s1);
    link_ = bottom->add<ui::Button>(story_.url.empty() ? "Link" : "Article", [this] { openArticle(); }, ui::Button::Kind::Secondary);
    pageLabel_ = bottom->add<ui::Label>("", Type::Caption);  // ink: it changes with the page, in the page's Fast pass, and is not settled
    pageLabel_->align = ui::TextAlign::Center;
    pageLabel_->flex = 1;
    next_ = bottom->add<ui::Button>("Next thread", [this] { nextThread(); }, ui::Button::Kind::Secondary);
    next_->enabled = false;
    root.add<ui::Spacer>(ui::s1);
    showLoading("Loading comments...");
  }

  void onShow() override {
    ctx().store.set("stack", overArticle_ ? "at" : "t");
    if (!resume_.chain.empty() && !story_.url.empty()) {  // the article was open over it when the app was left: straight to it
      Resume next = resume_;
      next.chain.erase(0, 1);
      resume_.chain.clear();
      ctx().nav.push<ArticleScreen>(story_, true, next);  // (the comments are read when the user comes back to them)
      return;
    }
    if (!hasText_ && !busy_) load();
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
          text_ = std::move(r.text);
          if (text_->paras.empty()) {
            busy_ = false;
            hasText_ = true;
            block_->setChapter({}, {}, ui::TextPos{});
            showMessage(Icon::MessageCircle, "No comments yet");
            showPage();
            return;
          }
          const ui::TextPos pos = posOf(*text_, at_);  // the start, or the place the story was left at
          at_ = Anchor{};
          showText(std::move(text_->paras), pos);  // (text_ keeps where the comments start)
        });
  }

  // The title and the line under it as read just now (the points and the title can have changed since the list).
  void showStory() {
    meta_->setText(storyLine(story_, ctx().clock.now()));
    if (title_->text() == story_.title) return;
    title_->setText(story_.title);
    if (title_->host()) title_->host()->relayout();  // the title may take another number of lines
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

  void showPage() override {
    pageLabel_->setText(pageText(), Hint::Page);  // with the page: one pass, no settle after it
    setEnabled(next_, reading() && !counting_ && nextStart() >= 0);
  }

  // The paragraph of the first answer to the story that starts after the page on screen, -1 if there is none.
  int nextStart() {
    const ui::Page& page = block_->page();
    if (page.end) return -1;
    return nextTopLevel(*text_, page.next.byte > 0 ? page.next.para : page.next.para - 1);
  }

  void nextThread() {
    if (!reading() || counting_) return;
    const int para = nextStart();
    if (para < 0) return;
    block_->goTo(ui::TextPos{para, 0});
    showPage();
  }

  // The article: back to it when the comments were opened from it, else opened over them. A story without a link: its
  // page on the site as a QR code.
  void openArticle() {
    if (story_.url.empty()) openLinkSheet(ctx(), *link_, story_);
    else if (overArticle_) ctx().nav.pop();
    else ctx().nav.push<ArticleScreen>(story_, true);
  }

  Story story_;
  bool overArticle_;
  Anchor at_;                         // where to open the text once it is read (a story opened again)
  Resume resume_;
  std::shared_ptr<ThreadText> text_;  // null until a read ended; its paragraphs are in the block
  int comments_ = 0;
  ui::Label* title_ = nullptr;
  ui::Label* meta_ = nullptr;
  ui::Label* pageLabel_ = nullptr;
  ui::Button* link_ = nullptr;
  ui::Button* next_ = nullptr;
};

void pushThread(AppContext& ctx, const Story& story, bool overArticle, Resume resume) {
  ctx.nav.push<ThreadScreen>(story, overArticle, std::move(resume));
}

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
    const std::string stack = ctx().store.get("stack", "");
    const bool known = stack == "a" || stack == "at" || stack == "t" || stack == "ta";
    if (known && unpackList(ctx().store.get("open", ""), left, open) && open.size() == 1 && now >= left && now - left < kResumeSec) {
      const std::vector<int> at = splitInts(ctx().store.get("at", ""));
      if (at.size() == 3) resumeAt_.at = Anchor{static_cast<uint32_t>(at[0]), at[1], at[2]};
      const std::vector<int> article = splitInts(ctx().store.get("article", ""));
      if (article.size() == 2) resumeAt_.articleAt = ui::TextPos{article[0], article[1]};
      resumeAt_.chain = stack;
      resume_ = std::move(open);
    }
  }

  void onShow() override {
    shown_ = true;
    if (!resume_.empty()) {
      const Story story = resume_[0];
      resume_.clear();
      Resume next = resumeAt_;
      next.chain.erase(0, 1);
      if (resumeAt_.chain[0] == 'a' && !story.url.empty()) ctx().nav.push<ArticleScreen>(story, false, next);
      else pushThread(ctx(), story, false, next);  // (the list is read when the user comes back to it)
      return;
    }
    // No story is open: what was kept of the last one goes.
    ctx().store.set("open", "");
    ctx().store.set("stack", "");
    ctx().store.set("at", "");
    ctx().store.set("article", "");
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
    oneChange();
  }

  // The switcher, the rows and the status line changed together: one region, so one pass on the panel (three regions
  // are three passes that wait for each other: 280 ms instead of 70).
  void oneChange() {
    if (ui::Widget* all = status_->parent()) all->invalidate(Hint::Content);
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
          rows_[i]->onClick = [this, i] { open(pager_->page() * kRowsPerPage + i, false); };
          rows_[i]->onLongPress = [this, i] { open(pager_->page() * kRowsPerPage + i, true); };
        }
        pager_->onPage = [this](int) { showRows(); };
      } else if (!error_.empty()) {
        holder_->add<ui::EmptyState>(errorIcon(error_), "Hacker News not loaded", error_, "Retry", [this] { refresh(); })->flex = 1;
      } else if (busy_ || fetched_ == 0) {
        holder_->add<ui::EmptyState>(Icon::Hackernews, "Loading...")->flex = 1;
      } else {
        holder_->add<ui::EmptyState>(Icon::Hackernews, "No stories")->flex = 1;
      }
      if (holder_->frame.w > 0) {  // (in build() the first layout is still to come)
        holder_->layout();         // its own place stays: only what is in it changed, not the whole screen
        holder_->invalidate(Hint::Content);
      }
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
      if (shown_) {
        fill();
        oneChange();
      } else {
        stale_ = true;
      }
    });
  }

  // A story: the page it links to (its comments are a button away there); a story without a link (Ask HN), or a long
  // press: its comments.
  void open(int index, bool comments) {
    if (index < 0 || index >= static_cast<int>(stories_.size())) return;
    const Story& story = stories_[static_cast<size_t>(index)];
    if (story.url.empty() || comments) pushThread(ctx(), story, false, Resume{});
    else ctx().nav.push<ArticleScreen>(story, false);
  }

  Section section_ = Section::Top;
  std::vector<Story> resume_;  // the story to open again (one or none), until the first onShow
  Resume resumeAt_;
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
