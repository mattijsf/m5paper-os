// A small forgiving XML / XHTML tokenizer for EPUB files (OPF, NCX, nav and chapter documents).
// Not a validating parser: it never throws, never reads past the end, and gives up quietly on broken markup.
// Tag and attribute names are lower-cased; text has its entities decoded (numeric and the usual named ones).
#pragma once

#include <string>
#include <vector>

namespace pos::reader::xml {

struct Attr {
  std::string name, value;
};

struct Event {
  enum Type { Start, End, Text } type = Text;
  std::string name;          // Start / End: lower-case tag name, with any namespace prefix ("dc:title")
  std::vector<Attr> attrs;   // Start
  bool selfClosing = false;  // Start: <br/> (an End event follows it as well)
  std::string text;          // Text: decoded characters
  std::string attr(const std::string& n) const {
    for (const Attr& a : attrs)
      if (a.name == n) return a.value;
    return "";
  }
};

class Tokenizer {
 public:
  explicit Tokenizer(const std::string& source) : s_(source) {}
  // Next event; false at the end. Comments, processing instructions and DOCTYPE are skipped; CDATA becomes Text.
  bool next(Event& e);

 private:
  const std::string& s_;
  size_t i_ = 0;
  bool pendingEnd_ = false;
  std::string pendingName_;
};

// &amp; &#8217; &#x2019; &eacute; ... Unknown entities are kept as written.
std::string decodeEntities(const std::string& s);
// Local part of a prefixed name ("dc:title" -> "title").
std::string localName(const std::string& n);
// Appends a code point as UTF-8.
void appendUtf8(std::string& out, unsigned cp);

}  // namespace pos::reader::xml
