#include "ui/textblock.h"

#include <algorithm>

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
  fakeBold = bold;
  return type;
}

int CanvasMetrics::width(const std::string& utf8, Type type, bool bold, bool italic) {
  bool fake;
  Type face = faceFor(type, bold, italic, fake);
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
      else if (ch == 0x05) {
        int level = 1;
        if (i + 1 < line.size() && line[i + 1] >= '1' && line[i + 1] <= '3') level = line[++i] - '0';
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
  switch (p.heading) {
    case 1: return Type::ReadXL;
    case 2: return Type::ReadL;
    default: return Type::ReadM;
  }
}

size_t cpLen(const std::string& s, size_t i) {
  unsigned char b = static_cast<unsigned char>(s[i]);
  size_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
  return std::min(n, s.size() - i);
}

struct Piece {
  int start, end;  // byte range in the paragraph text, inside one run
  int run;
};
// A word: pieces that touch each other (a style change in the middle of a word, or punctuation in its own run).
using Group = std::vector<Piece>;

std::vector<Group> wordsFrom(const Paragraph& p, const std::string& plain, int from) {
  std::vector<int> runEnd;
  int acc = 0;
  for (const Run& r : p.runs) {
    acc += static_cast<int>(r.text.size());
    runEnd.push_back(acc);
  }
  std::vector<Group> out;
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
    if (joined && !out.empty()) out.back().push_back({i, j, run});
    else out.push_back(Group{{i, j, run}});
    i = j;
    joined = true;
  }
  return out;
}

}  // namespace

Page layoutPage(const std::vector<Paragraph>& paras, TextPos start, const TextStyle& st, TextMetrics& m) {
  Page page;
  page.start = start;
  int y = 0;
  int p = std::max(0, start.para);
  int off = start.byte;
  const int total = static_cast<int>(paras.size());
  int headingLines = 0, headingPara = -1;  // a heading placed last on the page moves to the next page with its text

  auto breakBefore = [&](int para, int byte) {
    page.next = TextPos{para, byte};
    if (headingLines > 0 && static_cast<int>(page.lines.size()) > headingLines) {
      page.lines.resize(page.lines.size() - headingLines);
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
    if (para.heading > 0 && off == 0 && !page.lines.empty()) y += kHeadingGapAbove;

    std::vector<Group> words = wordsFrom(para, plain, off);
    const bool indentFirst = para.heading == 0 && off == 0 && p > 0 && paras[p - 1].heading == 0;
    bool firstLine = true;
    int placed = 0;
    size_t wi = 0;
    while (wi < words.size()) {
      Line line;
      line.y = y;
      line.h = pitch;
      int x = (firstLine && indentFirst) ? st.indent : 0;
      const int lineStart = words[wi][0].start;
      size_t used = 0;
      bool cutWord = false;
      while (wi + used < words.size()) {
        const Group& grp = words[wi + used];
        int gw = 0;
        for (const Piece& pc : grp) {
          const Run& r = para.runs[pc.run];
          gw += m.width(plain.substr(pc.start, pc.end - pc.start), base, r.bold || headingBold, r.italic);
        }
        const Run& r0 = para.runs[grp[0].run];
        const int sp = used == 0 ? 0 : m.width(" ", base, r0.bold || headingBold, r0.italic);
        if (used > 0 && x + sp + gw > st.width) break;
        if (used == 0 && x + gw > st.width) {
          if (grp.size() > 1) {
            // Too wide even alone: allow breaks between its pieces.
            Group copy = grp;
            words.erase(words.begin() + wi);
            for (size_t k = 0; k < copy.size(); ++k) words.insert(words.begin() + wi + k, Group{copy[k]});
            continue;
          }
          // A single word wider than the column: cut it by characters; the rest starts the next line.
          const Piece& pc = grp[0];
          const bool bold = r0.bold || headingBold;
          const std::string word = plain.substr(pc.start, pc.end - pc.start);
          size_t k = 0;
          std::string part;
          while (k < word.size()) {
            const size_t n = cpLen(word, k);
            if (!part.empty() && x + m.width(part + word.substr(k, n), base, bold, r0.italic) > st.width) break;
            part += word.substr(k, n);
            k += n;
          }
          line.segs.push_back(Seg{part, base, bold, r0.italic, x});
          words[wi][0].start += static_cast<int>(part.size());
          cutWord = true;
          break;
        }
        // Add the pieces: neighbours of the same style share a segment.
        int px = x + sp;
        for (size_t k = 0; k < grp.size(); ++k) {
          const Run& r = para.runs[grp[k].run];
          const bool bold = r.bold || headingBold;
          const std::string text = plain.substr(grp[k].start, grp[k].end - grp[k].start);
          const std::string lead = (k == 0 && used > 0) ? " " : "";
          if (!line.segs.empty() && line.segs.back().bold == bold && line.segs.back().italic == r.italic) {
            line.segs.back().text += lead + text;
          } else {
            line.segs.push_back(Seg{lead + text, base, bold, r.italic, k == 0 ? x : px});
          }
          px += m.width(text, base, bold, r.italic);
        }
        x += sp + gw;
        ++used;
      }
      if (!cutWord) wi += used;
      if (y + pitch > st.height && !page.lines.empty()) {
        if (para.heading > 0 && placed > 0 && off == 0 && static_cast<int>(page.lines.size()) > placed) {
          page.lines.resize(page.lines.size() - placed);  // a heading is not split across pages
          return breakBefore(p, 0);
        }
        return breakBefore(p, lineStart);
      }
      page.lines.push_back(std::move(line));
      y += pitch;
      ++placed;
      firstLine = false;
      if (headingLines > 0 && para.heading == 0 && placed == 1) headingLines = 0;  // the heading now has text after it
    }
    if (para.heading > 0) {
      headingLines = placed;
      headingPara = p;
    }
    y += st.paragraphGap;
    ++p;
    off = 0;
    if (y >= st.height && p < total) return breakBefore(p, 0);
  }
  page.next = TextPos{total, 0};
  page.end = true;
  return page;
}

std::vector<TextPos> paginate(const std::vector<Paragraph>& paras, const TextStyle& st, TextMetrics& m) {
  std::vector<TextPos> starts;
  TextPos pos{0, 0};
  const TextPos endPos{static_cast<int>(paras.size()), 0};
  starts.push_back(pos);
  for (int guard = 0; guard < 1000000; ++guard) {
    Page pg = layoutPage(paras, pos, st, m);
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
  invalidate(Hint::Screen);
}

void TextBlock::setStyle(const TextStyle& s) {
  style_ = s;
  startsValid_ = pageValid_ = false;
  invalidate(Hint::Screen);
}

void TextBlock::layout() {
  if (style_.width != frame.w || style_.height != frame.h) {
    style_.width = frame.w;
    style_.height = frame.h;
    startsValid_ = pageValid_ = false;
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
}

void TextBlock::goTo(TextPos pos) {
  const auto& s = pageStarts();
  pos_ = s[pageIndexOf(s, pos)];
  pageValid_ = false;
  invalidate(Hint::Content);
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
  invalidate(Hint::Content);
  return true;
}

bool TextBlock::prevPage() {
  const auto& s = pageStarts();
  int i = pageIndexOf(s, pos_);
  if (i <= 0) return false;
  pos_ = s[i - 1];
  pageValid_ = false;
  invalidate(Hint::Content);
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

void TextBlock::draw(Canvas& c) {
  const Page& pg = page();
  for (const Line& line : pg.lines) {
    for (const Seg& seg : line.segs) {
      bool fake;
      const Type face = CanvasMetrics::faceFor(seg.type, seg.bold, seg.italic, fake);
      drawText(c, seg.text, frame.x + seg.x, frame.y + line.y, face, ink, Anchor::TopLeft, bg());
      if (fake) drawText(c, seg.text, frame.x + seg.x + 1, frame.y + line.y, face, ink, Anchor::TopLeft, ink);  // 2nd strike: no background fill
    }
  }
}

}  // namespace pos::ui
