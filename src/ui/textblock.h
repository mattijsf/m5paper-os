// TextBlock: paginated rich text (paragraphs with bold / italic runs and headings) for the reader and
// for message / mail detail. docs/UI.md "Widgets", docs/apps/reader.md.
//
// Pagination is pure logic over a TextMetrics (so it is unit-tested with a fake monospace font and runs
// on the worker thread for whole books): layoutPage() lays out ONE page from a position; paginate() walks
// a whole text. A position is (paragraph, byte offset inside the paragraph's text), independent of font
// size, so a reading position survives a change of layout.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/text.h"
#include "ui/widget.h"

namespace pos::ui {

struct Run {
  std::string text;
  bool bold = false, italic = false;
};

struct Paragraph {
  std::vector<Run> runs;
  int heading = 0;        // 0 = body text, 1..3 = heading level
  bool softBreak = false;  // starts after a line break inside the same block (<br>): no indent, no gap above (verse, addresses)
};

struct TextPos {
  int para = 0;
  int byte = 0;  // offset into the paragraph's concatenated run text
  bool operator==(const TextPos& o) const { return para == o.para && byte == o.byte; }
  bool operator!=(const TextPos& o) const { return !(*this == o); }
  bool operator<(const TextPos& o) const { return para != o.para ? para < o.para : byte < o.byte; }
};

struct TextStyle {
  Type body = Type::ReadM;   // ReadS / ReadM / ReadL / ReadXL
  float lineSpacing = 1.35f; // line pitch = font line height x this
  int width = 492;           // text column
  int height = 700;          // page height
  int indent = 24;           // first-line indent of body paragraphs (not after a heading, not the first)
  int paragraphGap = 10;     // extra space after a paragraph
};

// Text measuring, abstracted so layout does not need a canvas (tests, worker).
class TextMetrics {
 public:
  virtual ~TextMetrics() = default;
  virtual int width(const std::string& utf8, Type type, bool bold, bool italic) = 0;
  virtual int lineHeight(Type type) = 0;
};

// Real metrics from the baked fonts (bold and italic exist at ReadM only; elsewhere bold is a double strike
// one pixel apart, italic is the regular face).
class CanvasMetrics : public TextMetrics {
 public:
  explicit CanvasMetrics(Canvas& c) : c_(c) {}
  int width(const std::string& utf8, Type type, bool bold, bool italic) override;
  int lineHeight(Type type) override;
  // The font used for a style (what draw uses).
  static Type faceFor(Type type, bool bold, bool italic, bool& fakeBold);

 private:
  Canvas& c_;
};

struct Seg {
  std::string text;
  Type type = Type::ReadM;
  bool bold = false, italic = false;
  int x = 0;  // from the left edge of the column
};
struct Line {
  std::vector<Seg> segs;
  int y = 0, h = 0;  // top and height (line pitch) inside the page
};
struct Page {
  std::vector<Line> lines;
  TextPos start, next;
  bool end = false;  // the page reaches the end of the text
};

// Lays out the page that starts at `start`. buildLines = false only works out where the page ends (page.next, page.end):
// no lines or segments are made, which is what pagination of a whole book needs and is several times faster.
Page layoutPage(const std::vector<Paragraph>& paras, TextPos start, const TextStyle& style, TextMetrics& m, bool buildLines = true);
// Start positions of every page of the text (always at least one).
// `keepGoing` (optional) is called once per page; return false to stop early (the result is then incomplete): the worker
// uses it to yield the CPU now and then and to cancel.
std::vector<TextPos> paginate(const std::vector<Paragraph>& paras, const TextStyle& style, TextMetrics& m,
                              const std::function<bool()>& keepGoing = {});
// Index of the page that contains `pos` (the last page that starts at or before it).
int pageIndexOf(const std::vector<TextPos>& starts, TextPos pos);

// Paragraph text without styling.
std::string plainText(const Paragraph& p);
// The reader's compact chapter format: one paragraph per line; control bytes 0x01/0x02 bold on/off,
// 0x03/0x04 italic on/off, 0x05 + a level digit character '1'..'3' starts a heading line, 0x06 marks a line that continues the
// previous block after a line break (Paragraph::softBreak). Blank lines are skipped.
std::vector<Paragraph> parseMarked(const std::string& text);

// Widget: shows one page at a time.
class TextBlock : public Widget {
 public:
  void setParagraphs(std::vector<Paragraph> paras);
  void setStyle(const TextStyle& s);  // width/height are taken from the frame (minus the insets) at layout time
  // Space between the frame and the text; touches still reach the widget there (the reader turns pages by zones).
  void setInsets(int l, int t, int r, int b);
  const TextStyle& style() const { return style_; }
  // Pages: computed on first use (synchronously: for a whole book compute them on the worker and hand them over).
  const std::vector<TextPos>& pageStarts();
  void setPageStarts(std::vector<TextPos> starts);
  // Replaces the text, its page starts (computed elsewhere, e.g. on the worker) and the shown position in one step;
  // one Content refresh. For chapter changes in the reader.
  void setChapter(std::vector<Paragraph> paras, std::vector<TextPos> starts, TextPos pos);
  int pageCount() { return static_cast<int>(pageStarts().size()); }
  int pageIndex() { return pageIndexOf(pageStarts(), pos_); }
  TextPos position() const { return pos_; }
  // Shows the page containing `pos` (a reading position survives re-layout).
  void goTo(TextPos pos);
  bool nextPage();   // false at the last page
  bool prevPage();   // false at the first page
  bool atEnd();
  const Page& page();  // the current page's layout (computed when needed)

  std::function<void()> onLayout;           // after the frame (so the page size) is known: update "Page 1 of N" here
  std::function<void(int)> onPageChange;  // called with the new page index after a turn by touch
  bool onTouch(const Gesture& g) override;  // tap on the left third = back, elsewhere = forward; horizontal swipes
  Size measure(int availW) override { return {availW, 0}; }
  void layout() override;
  void draw(Canvas& c) override;

 private:
  void relayout();
  std::vector<Paragraph> paras_;
  TextStyle style_;
  int padL_ = 0, padT_ = 0, padR_ = 0, padB_ = 0;
  TextPos pos_;
  std::vector<TextPos> starts_;
  bool startsValid_ = false;
  Page page_;
  bool pageValid_ = false;
};

}  // namespace pos::ui
