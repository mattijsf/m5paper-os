#include "apps/reader/library.h"

#include <algorithm>
#include <cstdint>

#include "core/fs.h"
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

// Size of a JPEG (from its SOF marker) or PNG (from IHDR); false if it is neither.
bool imageSize(const uint8_t* p, size_t n, bool& png, int& w, int& h) {
  png = n > 24 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G';
  if (png) {
    w = static_cast<int>((p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19]);
    h = static_cast<int>((p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23]);
    return w > 0 && h > 0;
  }
  if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
  size_t i = 2;
  while (i + 9 < n) {
    if (p[i] != 0xFF) {
      ++i;
      continue;
    }
    const uint8_t m = p[i + 1];
    if (m == 0xFF) {
      ++i;
      continue;
    }
    if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
      i += 2;
      continue;
    }
    const size_t len = static_cast<size_t>(p[i + 2] << 8 | p[i + 3]);
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      h = p[i + 5] << 8 | p[i + 6];
      w = p[i + 7] << 8 | p[i + 8];
      return w > 0 && h > 0;
    }
    i += 2 + len;
  }
  return false;
}

// Decodes a JPEG or PNG cover and scales it (box filter) to fit a 120 x 180 gray canvas, centred on white. nullptr if it cannot
// be read. The decode goes to a canvas of its own: JPEGs are decoded at 1/2, 1/4 or 1/8 size when they are big.
std::shared_ptr<Canvas> makeThumb(const std::string& virtualPath) {
  std::string data;
  if (!fs::readFile(virtualPath, data)) return nullptr;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
  bool png;
  int w, h;
  if (!imageSize(p, data.size(), png, w, h) || static_cast<int64_t>(w) * h > 12000000) return nullptr;
  int div = 1;
  if (!png)
    while (div < 8 && static_cast<int64_t>(w / div) * (h / div) > 1500000) div *= 2;
  const int sw = std::max(1, w / div), sh = std::max(1, h / div);
  Canvas src;
  src.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!src.createSprite(sw, sh)) return nullptr;
  src.fillSprite(static_cast<uint32_t>(0xFFFFFFu));
  const uint32_t len = static_cast<uint32_t>(data.size());
  const bool ok = png ? src.drawPng(p, len, 0, 0, sw, sh, 0, 0, 1.0f, 1.0f) : src.drawJpg(p, len, 0, 0, sw, sh, 0, 0, 1.0f / div, 1.0f / div);
  if (!ok) return nullptr;

  auto cv = std::make_shared<Canvas>();
  cv->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!cv->createSprite(kCoverW, kCoverH)) return nullptr;
  cv->fillSprite(static_cast<uint32_t>(0xFFFFFFu));
  const float f = std::min((kCoverW - 2) / static_cast<float>(sw), (kCoverH - 2) / static_cast<float>(sh));
  const int ow = std::max(1, static_cast<int>(sw * f)), oh = std::max(1, static_cast<int>(sh * f));
  const int ox = (kCoverW - ow) / 2, oy = (kCoverH - oh) / 2;
  const uint8_t* sbuf = static_cast<const uint8_t*>(src.getBuffer());
  uint8_t* dbuf = static_cast<uint8_t*>(cv->getBuffer());
  if (!sbuf || !dbuf) return nullptr;
  for (int y = 0; y < oh; ++y) {
    const int y0 = y * sh / oh, y1 = std::max(y0 + 1, (y + 1) * sh / oh);
    for (int x = 0; x < ow; ++x) {
      const int x0 = x * sw / ow, x1 = std::max(x0 + 1, (x + 1) * sw / ow);
      uint32_t sum = 0;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) sum += sbuf[static_cast<size_t>(yy) * sw + xx];
      dbuf[static_cast<size_t>(oy + y) * kCoverW + ox + x] = static_cast<uint8_t>(sum / static_cast<uint32_t>((y1 - y0) * (x1 - x0)));
    }
  }
  return cv;
}

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

void LibraryScreen::onShow() { refresh(); }  // also after coming back from a book: progress and order changed

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
  };
  ctx().async(
      [] {
        Scan s;
        s.entries = scanLibrary();
        if (!s.entries.empty() && s.entries[0].opened > 0 && !s.entries[0].cover.empty()) s.thumb = makeThumb(s.entries[0].cover);
        return s;
      },
      [this](Scan s) {
        entries_ = std::move(s.entries);
        thumb_ = std::move(s.thumb);
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
    if (!e.author.empty()) text->add<ui::Label>(e.author, Type::Body, ui::secondary);
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
    if (e.opened > 0) item->setTrailingText(std::to_string(e.percent) + "%");
    else item->clearTrailing();
    item->onClick = [this, file = e.file] { open_(file); };
  }
  if (list_ && list_->frame.w > 0) list_->layout();  // the card and rows came and went: positions change
}

}  // namespace pos::reader
