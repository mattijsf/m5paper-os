#include "apps/reader/xml.h"

#include "core/text_clean.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace pos::reader::xml {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
bool nameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.';
}

}  // namespace

void appendUtf8(std::string& out, unsigned cp) { pos::appendUtf8(out, cp); }

std::string decodeEntities(const std::string& s) { return pos::decodeEntities(s); }

std::string localName(const std::string& n) {
  const size_t c = n.rfind(':');
  return c == std::string::npos ? n : n.substr(c + 1);
}

bool Tokenizer::next(Event& e) {
  e = Event{};
  if (pendingEnd_) {
    pendingEnd_ = false;
    e.type = Event::End;
    e.name = pendingName_;
    return true;
  }
  const size_t n = s_.size();
  while (i_ < n) {
    if (s_[i_] != '<') {
      const size_t lt = s_.find('<', i_);
      const size_t end = lt == std::string::npos ? n : lt;
      e.type = Event::Text;
      e.text = decodeEntities(s_.substr(i_, end - i_));
      i_ = end;
      return true;
    }
    // A markup construct.
    if (s_.compare(i_, 4, "<!--") == 0) {
      const size_t close = s_.find("-->", i_ + 4);
      i_ = close == std::string::npos ? n : close + 3;
      continue;
    }
    if (s_.compare(i_, 9, "<![CDATA[") == 0) {
      const size_t close = s_.find("]]>", i_ + 9);
      const size_t end = close == std::string::npos ? n : close;
      e.type = Event::Text;
      e.text = s_.substr(i_ + 9, end - i_ - 9);
      i_ = close == std::string::npos ? n : close + 3;
      return true;
    }
    if (i_ + 1 < n && (s_[i_ + 1] == '?' || s_[i_ + 1] == '!')) {  // <?xml ?>, <!DOCTYPE ... [ ... ]>
      size_t j = i_ + 2;
      int depth = 0;
      while (j < n && !(s_[j] == '>' && depth <= 0)) {
        if (s_[j] == '[') ++depth;
        else if (s_[j] == ']') --depth;
        ++j;
      }
      i_ = j < n ? j + 1 : n;
      continue;
    }
    if (i_ + 1 < n && s_[i_ + 1] == '/') {  // end tag
      size_t j = i_ + 2;
      std::string name;
      while (j < n && nameChar(s_[j])) name += lowerAscii(s_[j++]);
      const size_t gt = s_.find('>', j);
      i_ = gt == std::string::npos ? n : gt + 1;
      if (name.empty()) continue;
      e.type = Event::End;
      e.name = name;
      return true;
    }
    if (i_ + 1 < n && nameChar(s_[i_ + 1])) {  // start tag
      size_t j = i_ + 1;
      std::string name;
      while (j < n && nameChar(s_[j])) name += lowerAscii(s_[j++]);
      e.type = Event::Start;
      e.name = name;
      bool closed = false;
      while (j < n) {
        while (j < n && isSpace(s_[j])) ++j;
        if (j >= n) break;
        if (s_[j] == '>') {
          ++j;
          break;
        }
        if (s_[j] == '/') {
          closed = true;
          ++j;
          continue;
        }
        Attr a;
        while (j < n && !isSpace(s_[j]) && s_[j] != '=' && s_[j] != '>' && s_[j] != '/') a.name += lowerAscii(s_[j++]);
        while (j < n && isSpace(s_[j])) ++j;
        if (j < n && s_[j] == '=') {
          ++j;
          while (j < n && isSpace(s_[j])) ++j;
          if (j < n && (s_[j] == '"' || s_[j] == '\'')) {
            const char q = s_[j++];
            const size_t close = s_.find(q, j);
            const size_t end = close == std::string::npos ? n : close;
            a.value = decodeEntities(s_.substr(j, end - j));
            j = close == std::string::npos ? n : close + 1;
          } else {
            size_t k = j;
            while (k < n && !isSpace(s_[k]) && s_[k] != '>') ++k;
            a.value = decodeEntities(s_.substr(j, k - j));
            j = k;
          }
        }
        if (!a.name.empty()) e.attrs.push_back(std::move(a));
        else if (j < n && s_[j] != '>' && s_[j] != '/') ++j;  // a stray character: skip it, never loop
      }
      i_ = j;
      if (closed) {
        e.selfClosing = true;
        pendingEnd_ = true;
        pendingName_ = name;
      }
      return true;
    }
    // A lone '<' that starts nothing: text.
    e.type = Event::Text;
    e.text = "<";
    ++i_;
    return true;
  }
  return false;
}

}  // namespace pos::reader::xml
