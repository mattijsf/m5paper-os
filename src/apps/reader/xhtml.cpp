#include "apps/reader/xhtml.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

#include "apps/reader/xml.h"
#include "core/strings.h"
#include "core/utf8.h"

namespace pos::reader {

namespace {

using xml::Event;

bool in(const std::string& n, std::initializer_list<const char*> names) {
  for (const char* x : names)
    if (n == x) return true;
  return false;
}

bool isBlock(const std::string& n) {
  return in(n, {"p", "div", "h1", "h2", "h3", "h4", "h5", "h6", "li", "ul", "ol", "blockquote", "pre", "section", "article", "aside",
                "header", "footer", "nav", "figure", "figcaption", "table", "tr", "thead", "tbody", "tfoot", "caption", "dl", "dt",
                "dd", "body", "html", "address", "form", "fieldset", "main", "center", "hgroup"});
}

// Alt text that says something: at least two letters in a row and not one of the generic words.
bool realAlt(const std::string& alt) {
  std::string low = lowerAscii(alt);
  while (!low.empty() && low.back() == ' ') low.pop_back();
  static const char* kGeneric[] = {"", "image", "img", "cover", "cover image", "cover page", "picture", "photo", "illustration", "logo", "decoration", "ornament", "figure", "frontispiece", "titlepage", "title page"};
  for (const char* g : kGeneric)
    if (low == g) return false;
  int letters = 0;
  for (char c : low)
    if (std::isalpha(static_cast<unsigned char>(c)) || static_cast<unsigned char>(c) >= 0x80) ++letters;
  return letters >= 4 && alt.size() <= 200;
}

class Converter {
 public:
  XhtmlResult run(const std::string& doc) {
    xml::Tokenizer tk(doc);
    Event e;
    while (tk.next(e)) {
      if (e.type == Event::Start) {
        start(e);
        if (skip_ == 0)
          for (const char* key : {"id", "name"}) {
            const std::string v = e.attr(key);
            if (!v.empty()) res_.anchors.emplace_back(v, res_.marked.size());
          }
      }
      else if (e.type == Event::End) end(e);
      else text(e.text);
    }
    flush();
    return std::move(res_);
  }

 private:
  struct ListState {
    bool ordered;
    int n;
  };

  void start(const Event& e) {
    const std::string& n = e.name;
    if (in(n, {"head", "style", "script", "title"})) {
      ++skip_;
      return;
    }
    if (n == "svg") {  // pictures are not shown (docs/apps/reader.md): no placeholder either
      ++skip_;
      return;
    }
    if (skip_ > 0) return;
    if (n == "br") {
      flush();
      soft_ = true;  // the next line continues this block: no indent, no gap
      return;
    }
    if (n == "hr") {
      flush();
      addText("* * *");
      flush();
      return;
    }
    if (n == "img") {
      // No placeholder: a page with only "[image]" is wasted. The alt text stays when it is real text (a caption).
      const std::string alt = e.attr("alt");
      if (realAlt(alt)) addText(alt);
      return;
    }
    if (n == "b" || n == "strong") {
      ++bold_;
      return;
    }
    if (n == "i" || n == "em" || n == "cite" || n == "dfn" || n == "var") {
      ++italic_;
      return;
    }
    if (n == "td" || n == "th") {  // each cell is a line of its own, lines of a row stay together
      flush();
      soft_ = rowCells_++ > 0;
      return;
    }
    if (isBlock(n)) {
      flush();
      soft_ = false;
      if (n == "tr") rowCells_ = 0;
      if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6') heading_ = std::min(3, n[1] - '0');
      else if (n == "ul" || n == "ol") lists_.push_back({n == "ol", 0});
      else if (n == "li") {
        std::string bullet = "\xE2\x80\xA2 ";
        if (!lists_.empty() && lists_.back().ordered) bullet = std::to_string(++lists_.back().n) + ". ";
        addText(bullet);
      }
    }
  }

  void end(const Event& e) {
    const std::string& n = e.name;
    if (in(n, {"head", "style", "script", "title", "svg"})) {
      if (skip_ > 0) --skip_;
      return;
    }
    if (skip_ > 0) return;
    if (n == "b" || n == "strong") {
      if (bold_ > 0) --bold_;
      return;
    }
    if (n == "i" || n == "em" || n == "cite" || n == "dfn" || n == "var") {
      if (italic_ > 0) --italic_;
      return;
    }
    if (isBlock(n)) {
      flush();
      soft_ = false;
      if ((n == "ul" || n == "ol") && !lists_.empty()) lists_.pop_back();
    }
  }

  void text(const std::string& t) {
    if (skip_ > 0) return;
    addText(t);
  }

  // Appends text to the current paragraph: control bytes dropped, white space collapsed, style marks emitted on change.
  void addText(const std::string& t) {
    for (size_t i = 0; i < t.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(t[i]);
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
        if (hasText_ && !lastSpace_) {
          syncStyle();
          cur_ += ' ';
          lastSpace_ = true;
        }
        continue;
      }
      if (c < 0x20) continue;
      syncStyle();
      cur_ += static_cast<char>(c);
      hasText_ = true;
      lastSpace_ = false;
    }
  }

  // Makes the marks in the paragraph match bold_ / italic_ (a paragraph balances itself: see flush()).
  void syncStyle() {
    if ((bold_ > 0) != openBold_) {
      cur_ += openBold_ ? '\x02' : '\x01';
      openBold_ = !openBold_;
    }
    if ((italic_ > 0) != openItalic_) {
      cur_ += openItalic_ ? '\x04' : '\x03';
      openItalic_ = !openItalic_;
    }
  }

  void flush() {
    if (hasText_) {
      while (!cur_.empty() && cur_.back() == ' ') cur_.pop_back();
      if (openBold_) cur_ += '\x02';
      if (openItalic_) cur_ += '\x04';
      std::string line;
      if (soft_ && heading_ == 0) line += '\x06';
      soft_ = false;
      if (heading_ > 0) {
        line += '\x05';
        line += static_cast<char>('0' + heading_);
        if (res_.firstHeading.empty()) {
          std::string plain;
          for (char ch : cur_)
            if (static_cast<unsigned char>(ch) >= 0x20) plain += ch;
          res_.firstHeading = utf8::truncate(plain, 120);
        }
      }
      line += cur_;
      res_.marked += line;
      res_.marked += '\n';
    }
    cur_.clear();
    hasText_ = false;
    lastSpace_ = false;
    heading_ = 0;
    openBold_ = openItalic_ = false;  // the next paragraph starts clean; syncStyle re-opens what is still active
  }

  XhtmlResult res_;
  std::string cur_;
  bool hasText_ = false, lastSpace_ = false, openBold_ = false, openItalic_ = false, soft_ = false;
  int bold_ = 0, italic_ = 0, heading_ = 0, skip_ = 0, rowCells_ = 0;
  std::vector<ListState> lists_;
};

}  // namespace

XhtmlResult xhtmlToMarked(const std::string& xhtml) {
  Converter c;
  return c.run(xhtml);
}

}  // namespace pos::reader
