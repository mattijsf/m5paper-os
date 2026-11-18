#include "ui/textblock.h"

#include <cstring>

#include <algorithm>

#include "core/utf8.h"
#include "ui/root.h"

namespace pos::ui {

// ---- metrics

Type CanvasMetrics::faceFor(Type type, bool bold, bool italic, bool& fakeBold) {
  fakeBold = false;
  if (type == Type::ReadM) {
    if (bold) return Type::ReadBoldM;
    if (italic) return Type::ReadItalicM;
    return Type::ReadM;
  }
  // Text set in the interface's font (a comment thread, an article): its bold is a face of its own, its headings are.
  if (type == Type::Body) return bold ? Type::BodyStrong : Type::Body;
  if (type == Type::Title || type == Type::Headline) return type;
  fakeBold = bold;
  return type;
}

int CanvasMetrics::width(const std::string& utf8, Type type, bool bold, bool italic) {
  bool fake;
  Type face = faceFor(type, bold, italic, fake);
  // The interface's font: what drawing moves the pen by (ui::penAdvance says why), so a bold word in a line stands where
  // the text before it ends. The reader's faces measure as they draw.
  if (face == Type::Body || face == Type::BodyStrong || face == Type::Title || face == Type::Headline) return penAdvance(c_, utf8, face);
  return measureText(c_, utf8, face) + (fake ? 1 : 0);
}

int CanvasMetrics::lineHeight(Type type) { return ui::lineHeight(c_, type); }

// ---- text helpers

std::string plainText(const Paragraph& p) {
  std::string s;
  for (const Run& r : p.runs) s += r.text;
  return s;
}

std::vector<Paragraph> parseMarked(const std::string& text) {
  std::vector<Paragraph> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    Paragraph p;
    Run cur;
    bool bold = false, italic = false;
    auto flush = [&] {
      if (!cur.text.empty()) p.runs.push_back(cur);
      cur = Run{};
      cur.bold = bold;
      cur.italic = italic;
    };
    for (size_t i = 0; i < line.size(); ++i) {
      const unsigned char ch = static_cast<unsigned char>(line[i]);
      if (ch == 0x01) { flush(); bold = true; cur.bold = true; }
      else if (ch == 0x02) { flush(); bold = false; cur.bold = false; }
      else if (ch == 0x03) { flush(); italic = true; cur.italic = true; }
      else if (ch == 0x04) { flush(); italic = false; cur.italic = false; }
      else if (ch == 0x06) p.softBreak = true;
      else if (ch == 0x05) {
        int level = 1;
        if (i + 1 < line.size() && line[i + 1] >= '1' && line[i + 1] <= '4') level = line[++i] - '0';
        p.heading = level;
      } else cur.text += line[i];
    }
    flush();
    if (!p.runs.empty()) out.push_back(std::move(p));
    if (end == text.size()) break;
  }
  return out;
}

namespace {

constexpr int kHeadingGapAbove = 18;

Type typeFor(const Paragraph& p, const TextStyle& st, bool& bold) {
  bold = false;
  if (p.heading == 0) return st.body;
  bold = true;
  if (st.body == Type::Body) return p.heading == 1 ? Type::Headline : p.heading == 2 ? Type::Title : Type::Body;  // Inter
  switch (p.heading) {
    case 1: return Type::ReadXL;
    case 2: return Type::ReadL;
    case 3: return Type::ReadM;
    default: return st.body;  // 4: a byline
  }
}

struct Piece {
  int start, end;  // byte range in the paragraph text, inside one run
  int run;
};
// A word: pieces that touch each other (a style change in the middle of a word, or punctuation in its own run).
// Nearly every word is one piece, kept inline: pagination of a book makes millions of these.
struct Group {
  Piece first;
  std::vector<Piece> more;  // the pieces after the first
  size_t size() const { return 1 + more.size(); }
  const Piece& at(size_t k) const { return k == 0 ? first : more[k - 1]; }
};

std::vector<Group> wordsFrom(const Paragraph& p, const std::string& plain, int from) {
  std::vector<int> runEnd;
  int acc = 0;
  for (const Run& r : p.runs) {
    acc += static_cast<int>(r.text.size());
    runEnd.push_back(acc);
  }
  std::vector<Group> out;
  out.reserve(plain.size() / 6 + 4);
  int i = from;
  const int n = static_cast<int>(plain.size());
  bool joined = false;  // the previous piece touches this one
  while (i < n) {
    if (plain[i] == ' ' || plain[i] == '\t') {
      ++i;
      joined = false;
      continue;
    }
    int run = 0;
    while (run < static_cast<int>(runEnd.size()) - 1 && i >= runEnd[run]) ++run;
    int j = i;
    while (j < n && plain[j] != ' ' && plain[j] != '\t' && j < runEnd[run]) ++j;
    if (joined && !out.empty()) out.back().more.push_back({i, j, run});
    else out.push_back(Group{{i, j, run}, {}});
    i = j;
    joined = true;
  }
  return out;
}

}  // namespace

Page layoutPage(const std::vector<Paragraph>& paras, TextPos start, const TextStyle& st, TextMetrics& m, bool buildLines) {
  Page page;
  page.start = start;
  int y = 0;
  int p = std::max(0, start.para);
  int off = std::max(0, start.byte);  // a position from a file: never before the paragraph
  const int total = static_cast<int>(paras.size());
  int headingLines = 0, headingPara = -1;  // a heading placed last on the page moves to the next page with its text
  int nLines = 0;                           // lines on the page (page.lines only fills when buildLines)
  std::string scratch;                      // word text for measuring, reused

  auto dropLines = [&](int k) {
    nLines -= k;
    if (buildLines) page.lines.resize(page.lines.size() - k);
  };
  auto breakBefore = [&](int para, int byte) {
    page.next = TextPos{para, byte};
    if (headingLines > 0 && nLines > headingLines) {
      dropLines(headingLines);
      page.next = TextPos{headingPara, 0};
    }
    return page;
  };

  while (p < total) {
    const Paragraph& para = paras[p];
    const std::string plain = plainText(para);
    bool headingBold;
    const Type base = typeFor(para, st, headingBold);
    const int pitch = std::max(1, static_cast<int>(m.lineHeight(base) * st.lineSpacing + 0.5f));
    if (para.heading > 0 && off == 0 && nLines > 0) y += kHeadingGapAbove;

    // Width of a space in each style of this paragraph (bold x italic), measured once.
    int spaceW[4] = {-1, -1, -1, -1};
    auto spaceWidth = [&](bool bold, bool italic) {
      int& w = spaceW[(bold ? 2 : 0) + (italic ? 1 : 0)];
      if (w < 0) w = m.width(" ", base, bold, italic);
      return w;
    };
    auto pieceText = [&](const Piece& pc) -> const std::string& {
      scratch.assign(plain, static_cast<size_t>(pc.start), static_cast<size_t>(pc.end - pc.start));
      return scratch;
    };

    std::vector<Group> words = wordsFrom(para, plain, off);
    const bool indentFirst = para.heading == 0 && off == 0 && p > 0 && paras[p - 1].heading == 0 && !para.softBreak;
    bool firstLine = true;
    int placed = 0;
    size_t wi = 0;
    while (wi < words.size()) {
      Line line;
      line.y = y;
      line.h = pitch;
      int x = std::max(0, para.margin) + ((firstLine && indentFirst) ? st.indent : 0);
      const int lineStart = words[wi].first.start;
      size_t used = 0;
      bool cutWord = false;
      while (wi + used < words.size()) {
        const Group& grp = words[wi + used];
        int gw = 0;
        for (size_t k = 0; k < grp.size(); ++k) {
          const Piece& pc = grp.at(k);
          const Run& r = para.runs[pc.run];
          gw += m.width(pieceText(pc), base, r.bold || headingBold, r.italic);
        }
        const Run& r0 = para.runs[grp.first.run];
        const int sp = used == 0 ? 0 : spaceWidth(r0.bold || headingBold, r0.italic);
        if (used > 0 && x + sp + gw > st.width) break;
        if (used == 0 && x + gw > st.width) {
          if (grp.size() > 1) {
            // Too wide even alone: allow breaks between its pieces.
            Group copy = grp;
            words.erase(words.begin() + wi);
            for (size_t k = 0; k < copy.size(); ++k) words.insert(words.begin() + wi + k, Group{copy.at(k), {}});
            continue;
          }
          // A single word wider than the column: cut it by characters; the rest starts the next line.
          const Piece& pc = grp.first;
          const bool bold = r0.bold || headingBold;
          const std::string word = plain.substr(pc.start, pc.end - pc.start);
          size_t k = 0;
          std::string part;
          while (k < word.size()) {
            const size_t n = utf8::span(word, k);
            if (!part.empty() && x + m.width(part + word.substr(k, n), base, bold, r0.italic) > st.width) break;
            part += word.substr(k, n);
            k += n;
          }
          if (buildLines) line.segs.push_back(Seg{part, base, bold, r0.italic, x});
          words[wi].first.start += static_cast<int>(part.size());
          cutWord = true;
          break;
        }
        if (buildLines) {
          // Add the pieces: neighbours of the same style share a segment.
          int px = x + sp;
          for (size_t k = 0; k < grp.size(); ++k) {
            const Run& r = para.runs[grp.at(k).run];
            const bool bold = r.bold || headingBold;
            const std::string text = plain.substr(grp.at(k).start, grp.at(k).end - grp.at(k).start);
            const std::string lead = (k == 0 && used > 0) ? " " : "";
            if (!line.segs.empty() && line.segs.back().bold == bold && line.segs.back().italic == r.italic) {
              line.segs.back().text += lead + text;
            } else {
              line.segs.push_back(Seg{lead + text, base, bold, r.italic, k == 0 ? x : px});
            }
            px += m.width(text, base, bold, r.italic);
          }
        }
        x += sp + gw;
        ++used;
      }
      if (!cutWord) wi += used;
      if (y + pitch > st.height && nLines > 0) {
        if (para.heading > 0 && placed > 0 && off == 0 && nLines > placed) {
          dropLines(placed);  // a heading is not split across pages
          return breakBefore(p, 0);
        }
        return breakBefore(p, lineStart);
      }
      if (buildLines) page.lines.push_back(std::move(line));
      ++nLines;
      y += pitch;
      ++placed;
      firstLine = false;
      if (headingLines > 0 && para.heading == 0 && placed == 1) headingLines = 0;  // the heading now has text after it
    }
    if (para.heading > 0) {
      headingLines = placed;
      headingPara = p;
    }
    if (!(p + 1 < total && paras[p + 1].softBreak)) y += st.paragraphGap;  // a line break keeps the lines together
    ++p;
    off = 0;
    if (y >= st.height && p < total) return breakBefore(p, 0);
  }
  page.next = TextPos{total, 0};
  page.end = true;
  return page;
}

std::vector<TextPos> paginate(const std::vector<Paragraph>& paras, const TextStyle& st, TextMetrics& m,
                              const std::function<bool()>& keepGoing) {
  std::vector<TextPos> starts;
  TextPos pos{0, 0};
  const TextPos endPos{static_cast<int>(paras.size()), 0};
  starts.push_back(pos);
  for (int guard = 0; guard < 1000000; ++guard) {
    if (keepGoing && !keepGoing()) break;
    Page pg = layoutPage(paras, pos, st, m, false);
    if (pg.end || pg.next == pos || !(pos < pg.next) || pg.next == endPos) break;
    pos = pg.next;
    starts.push_back(pos);
  }
  return starts;
}

int pageIndexOf(const std::vector<TextPos>& starts, TextPos pos) {
  int idx = 0;
  for (size_t i = 0; i < starts.size(); ++i)
    if (!(pos < starts[i])) idx = static_cast<int>(i);
  return idx;
}

// ---- widget

void TextBlock::setParagraphs(std::vector<Paragraph> paras) {
  paras_ = std::move(paras);
  pos_ = TextPos{0, 0};
  startsValid_ = pageValid_ = false;
  ++gen_;
  invalidate(Hint::Screen);
}

void TextBlock::setStyle(const TextStyle& s) {
  style_ = s;
  startsValid_ = pageValid_ = false;
  ++gen_;
  invalidate(Hint::Screen);
}

void TextBlock::setInsets(int l, int t, int r, int b) {
  padL_ = l;
  padT_ = t;
  padR_ = r;
  padB_ = b;
  startsValid_ = pageValid_ = false;
  ++gen_;
}

void TextBlock::layout() {
  const int w = frame.w - padL_ - padR_, h = frame.h - padT_ - padB_;
  if (style_.width != w || style_.height != h) {
    style_.width = w;
    style_.height = h;
    startsValid_ = pageValid_ = false;
    ++gen_;
  }
  if (onLayout) onLayout();
}

const std::vector<TextPos>& TextBlock::pageStarts() {
  if (!startsValid_) {
    Canvas* c = canvas();
    if (c) {
      CanvasMetrics m(*c);
      starts_ = paginate(paras_, style_, m);
    } else {
      starts_ = {TextPos{0, 0}};
    }
    startsValid_ = true;
  }
  return starts_;
}

void TextBlock::setPageStarts(std::vector<TextPos> starts) {
  starts_ = std::move(starts);
  if (starts_.empty()) starts_.push_back(TextPos{0, 0});
  startsValid_ = true;
  pageValid_ = false;
  ++gen_;
}

void TextBlock::setChapter(std::vector<Paragraph> paras, std::vector<TextPos> starts, TextPos pos) {
  paras_ = std::move(paras);
  starts_ = std::move(starts);
  if (starts_.empty()) starts_.push_back(TextPos{0, 0});
  startsValid_ = true;
  ++gen_;
  pos_ = starts_[pageIndexOf(starts_, pos)];
  pageValid_ = false;
  invalidate(pageHint);
}

void TextBlock::goTo(TextPos pos) {
  const auto& s = pageStarts();
  pos_ = s[pageIndexOf(s, pos)];
  pageValid_ = false;
  invalidate(pageHint);
}

const Page& TextBlock::page() {
  if (!pageValid_) {
    Canvas* c = canvas();
    if (c) {
      CanvasMetrics m(*c);
      page_ = layoutPage(paras_, pos_, style_, m);
    } else {
      page_ = Page{};
    }
    pageValid_ = true;
  }
  return page_;
}

bool TextBlock::atEnd() { return page().end; }

bool TextBlock::nextPage() {
  const auto& s = pageStarts();
  int i = pageIndexOf(s, pos_);
  if (i + 1 >= static_cast<int>(s.size())) return false;
  pos_ = s[i + 1];
  pageValid_ = false;
  invalidate(pageHint);
  return true;
}

bool TextBlock::prevPage() {
  const auto& s = pageStarts();
  int i = pageIndexOf(s, pos_);
  if (i <= 0) return false;
  pos_ = s[i - 1];
  pageValid_ = false;
  invalidate(pageHint);
  return true;
}

bool TextBlock::onTouch(const Gesture& g) {
  bool turned = false;
  switch (g.type) {
    case Gesture::Down:
    case Gesture::Cancel:
    case Gesture::LongPress:
      return true;
    case Gesture::Tap:
      turned = g.startX < frame.x + frame.w / 3 ? prevPage() : nextPage();
      break;
    case Gesture::SwipeLeft:
      turned = nextPage();
      break;
    case Gesture::SwipeRight:
      turned = prevPage();
      break;
    default:
      return false;
  }
  if (turned && onPageChange) onPageChange(pageIndex());
  return true;
}

bool TextBlock::preUsable() {
  return pre_ && preValid_ && preGen_ == gen_ && preFrame_.x == frame.x && preFrame_.y == frame.y && preFrame_.w == frame.w &&
         preFrame_.h == frame.h && preBg_ == bg();
}

bool TextBlock::prefetched(TextPos pos) { return preUsable() && prePos_ == pos; }

void TextBlock::prefetch(TextPos pos) {
  Canvas* c = canvas();
  if (!c || frame.w <= 0 || frame.h <= 0) return;
  const auto& s = pageStarts();
  pos = s[pageIndexOf(s, pos)];
  if (prefetched(pos)) return;
  if (pre_ && (pre_->width() != frame.w || pre_->height() != frame.h)) pre_.reset();
  if (!pre_) {
    pre_.reset(new Canvas());
    pre_->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
    pre_->setPsram(true);
    if (!pre_->createSprite(frame.w, frame.h)) {
      pre_->setPsram(false);
      if (!pre_->createSprite(frame.w, frame.h)) {
        pre_.reset();
        return;
      }
    }
  }
  pre_->fillScreen(bg());
  CanvasMetrics m(*c);
  const Page pg = layoutPage(paras_, pos, style_, m);
  drawPage(*pre_, pg, -frame.x, -frame.y);
  prePos_ = pos;
  preGen_ = gen_;
  preFrame_ = frame;
  preBg_ = bg();
  preValid_ = true;
}

void TextBlock::drawPage(Canvas& c, const Page& pg, int ox, int oy) {
  for (const Line& line : pg.lines) {
    for (const Seg& seg : line.segs) {
      bool fake;
      const Type face = CanvasMetrics::faceFor(seg.type, seg.bold, seg.italic, fake);
      const int x = ox + frame.x + padL_ + seg.x, y = oy + frame.y + padT_ + line.y;
      drawText(c, seg.text, x, y, face, ink, Anchor::TopLeft, bg());
      if (fake) drawText(c, seg.text, x + 1, y, face, ink, Anchor::TopLeft, ink);  // 2nd strike: no background fill
    }
  }
}

void TextBlock::draw(Canvas& c) {
  if (prefetched(pos_)) {
    // The page was drawn ahead of time: copy the part of it inside the clip.
    int32_t cx, cy, cw, ch;
    c.getClipRect(&cx, &cy, &cw, &ch);
    const int x0 = std::max<int>(frame.x, cx), x1 = std::min<int>(frame.x + frame.w, cx + cw);
    const int y0 = std::max<int>(frame.y, cy), y1 = std::min<int>(frame.y + frame.h, cy + ch);
    auto* dst = static_cast<uint8_t*>(c.getBuffer());
    const auto* src = static_cast<const uint8_t*>(pre_->getBuffer());
    if (dst && src && c.getColorDepth() == lgfx::color_depth_t::grayscale_8bit) {
      for (int y = y0; y < y1 && x1 > x0; ++y)
        std::memcpy(dst + y * c.width() + x0, src + (y - frame.y) * frame.w + (x0 - frame.x), static_cast<size_t>(x1 - x0));
      return;
    }
  }
  int32_t cx, cy, cw, ch;
  c.getClipRect(&cx, &cy, &cw, &ch);
  const int x0 = std::max<int>(frame.x, cx), x1 = std::min<int>(frame.x + frame.w, cx + cw);
  const int y0 = std::max<int>(frame.y, cy), y1 = std::min<int>(frame.y + frame.h, cy + ch);
  if (x1 > x0 && y1 > y0) c.fillRect(x0, y0, x1 - x0, y1 - y0, bg());  // what Root would have done (transparent = true)
  drawPage(c, page(), 0, 0);
}

}  // namespace pos::ui
