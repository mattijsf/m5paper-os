#include "apps/reader/library.h"

#include <algorithm>
#include <cstdint>

#include "core/fs.h"
#include "ui/image.h"
#include "ui/root.h"

namespace pos::reader {

namespace {

using ui::Type;

constexpr int kRowsFirst = 5;  // page 1 also holds the "continue" card
constexpr int kRowsOther = 7;
constexpr int kCoverW = 120, kCoverH = 180;

// The cover thumbnail (120 x 180, decoded on the worker): the picture with a thin frame, or a framed box with a book icon
// when the book has no cover (yet).
class CoverView : public ui::Widget {
 public:
  explicit CoverView(std::shared_ptr<Canvas> thumb) : thumb_(std::move(thumb)) {}
  ui::Size measure(int) override { return {kCoverW, kCoverH}; }
  void draw(Canvas& c) override {
    if (thumb_) thumb_->pushSprite(&c, frame.x, frame.y);
    c.drawRect(frame.x, frame.y, frame.w, frame.h, ui::ink);
    if (!thumb_) {
      c.drawRect(frame.x + 1, frame.y + 1, frame.w - 2, frame.h - 2, ui::ink);
      ui::drawIcon(c, Icon::BookOpen, frame.x + frame.w / 2, frame.y + frame.h / 2, 64, ui::secondary, bg());
    }
  }

 private:
  std::shared_ptr<Canvas> thumb_;
};

class ProgressBar : public ui::Widget {
 public:
  explicit ProgressBar(int percent) : percent_(percent) {}
  ui::Size measure(int availW) override { return {availW, 12}; }
  void draw(Canvas& c) override {
    const int y = frame.y + 2, h = 8;
    c.fillRect(frame.x, y, frame.w, h, gray(12));
    c.fillRect(frame.x, y, frame.w * std::min(100, std::max(0, percent_)) / 100, h, ui::ink);
  }

 private:
  int percent_;
};

class Rule : public ui::Widget {
 public:
  ui::Size measure(int availW) override { return {availW, 1}; }
  void draw(Canvas& c) override { c.drawFastHLine(frame.x, frame.y, frame.w, ui::divider); }
};

}  // namespace

void LibraryScreen::build(ui::Column& root) {
  setTitle("Library");
  holder_ = root.add<ui::Column>(0);
  holder_->flex = 1;
  holder_->add<ui::EmptyState>(Icon::BookOpen, "Loading...", "")->flex = 1;
}

void LibraryScreen::onShow() {
  if (covered_) {  // the page screen comes over it in the same pass: scan when the reader comes back
    covered_ = false;
    return;
  }
  refresh();  // also after coming back from a book: progress and order changed
}

std::string LibraryScreen::debugState() const {
  if (!loaded_) return "loading";
  std::string s = "books=" + std::to_string(entries_.size());
  for (const LibraryEntry& e : entries_) s += "|" + e.title + "|" + e.author + "|" + std::to_string(e.percent);
  return s;
}

void LibraryScreen::refresh() {
  struct Scan {
    std::vector<LibraryEntry> entries;
    std::shared_ptr<Canvas> thumb;
    std::string thumbPath;
  };
  // The card's cover is decoded once per book: decoding it again on every return from a book kept the worker busy for
  // 2 to 3 s (JPEG of 220-270 KB), and a book tapped meanwhile opened that much later (device, 2026-11-16).
  const std::string havePath = thumbPath_;
  const std::shared_ptr<Canvas> have = thumb_;
  ctx().async(
      [havePath, have] {
        Scan s;
        s.entries = scanLibrary();
        if (!s.entries.empty() && s.entries[0].opened > 0 && !s.entries[0].cover.empty()) {
          s.thumbPath = s.entries[0].cover;
          s.thumb = (s.thumbPath == havePath && have) ? have : ui::loadImageFit(s.thumbPath, kCoverW, kCoverH);
        }
        return s;
      },
      [this](Scan s) {
        entries_ = std::move(s.entries);
        thumb_ = std::move(s.thumb);
        thumbPath_ = std::move(s.thumbPath);
        loaded_ = true;
        rebuild();
      });
}

void LibraryScreen::rebuild() {
  while (!holder_->children().empty()) holder_->remove(holder_->children().back().get());
  rows_.clear();
  others_.clear();
  pager_ = nullptr;
  list_ = nullptr;
  card_ = nullptr;
  cover_ = nullptr;
  if (entries_.empty()) {
    holder_->add<ui::EmptyState>(Icon::BookOpen, "No books yet", "Copy .epub or .txt files to the books folder on the SD card.")->flex = 1;
    holder_->host()->relayout();
    return;
  }
  const bool hasCard = entries_[0].opened > 0;
  for (size_t i = hasCard ? 1 : 0; i < entries_.size(); ++i) others_.push_back(entries_[i]);
  firstRows_ = hasCard ? kRowsFirst : kRowsOther;
  const int n = static_cast<int>(others_.size());
  const int pages = 1 + std::max(0, (n - firstRows_ + kRowsOther - 1) / kRowsOther);

  holder_->add<ui::Spacer>(ui::s2);
  pager_ = holder_->add<ui::Pager>(pages);
  pager_->flex = 1;
  pager_->bar()->visible = pages > 1;
  list_ = pager_->add<ui::Column>(0);
  auto* list = list_;
  if (hasCard) {
    const LibraryEntry e = entries_[0];
    card_ = list->add<ui::Column>(0);
    auto* row = card_->add<ui::Row>(ui::s3, ui::Align::Start);
    cover_ = row->add<CoverView>(thumb_);
    auto* text = row->add<ui::Column>(ui::s1, ui::Align::Stretch);
    text->flex = 1;
    text->add<ui::Label>(e.title, Type::Title)->maxLines = 2;
    if (!e.author.empty()) text->add<ui::Label>(e.author, Type::Body);  // content: ink
    text->add<ui::Spacer>(0, 1);
    text->add<ProgressBar>(e.percent);
    text->add<ui::Label>(std::to_string(e.percent) + "%  read", Type::Caption, ui::secondary);
    text->add<ui::Button>("Continue", [this, file = e.file] { open_(file); });
    card_->add<ui::Spacer>(ui::s2);
    card_->add<Rule>();
  }
  for (int i = 0; i < kRowsOther; ++i) rows_.push_back(list->add<ui::ListItem>());
  pager_->onPage = [this](int page) { fill(page); };
  fill(0);
  holder_->host()->relayout();
  if (cover_ && thumb_) cover_->invalidate(Hint::Image);  // a picture: the gray pass for its area
}

void LibraryScreen::fill(int page) {
  page_ = page;
  if (card_) card_->visible = page == 0;
  const int rowsHere = (page == 0) ? firstRows_ : kRowsOther;
  const int start = page == 0 ? 0 : firstRows_ + (page - 1) * kRowsOther;
  for (int i = 0; i < kRowsOther; ++i) {
    ui::ListItem* item = rows_[i];
    const int idx = start + i;
    const bool show = i < rowsHere && idx < static_cast<int>(others_.size());
    item->visible = show;
    if (!show) continue;
    const LibraryEntry& e = others_[idx];
    item->setTitle(e.title);
    item->setSubtitle(e.author);
    item->setSubtitleIsContent(true);
    if (e.opened > 0) item->setTrailingText(std::to_string(e.percent) + "%");
    else item->clearTrailing();
    item->onClick = [this, file = e.file] { open_(file); };
  }
  if (list_ && list_->frame.w > 0) list_->layout();  // the card and rows came and went: positions change
}

}  // namespace pos::reader
