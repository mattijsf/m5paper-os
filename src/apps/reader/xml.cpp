#include "apps/reader/xml.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace pos::reader::xml {

namespace {

struct Named {
  const char* name;
  unsigned cp;
};
const Named kNamed[] = {
    {"amp", '&'},      {"lt", '<'},       {"gt", '>'},       {"quot", '"'},     {"apos", '\''},    {"nbsp", 0xA0},    {"iexcl", 0xA1},
    {"cent", 0xA2},    {"pound", 0xA3},   {"curren", 0xA4},  {"yen", 0xA5},     {"brvbar", 0xA6},  {"sect", 0xA7},    {"uml", 0xA8},
    {"copy", 0xA9},    {"ordf", 0xAA},    {"laquo", 0xAB},   {"not", 0xAC},     {"shy", 0xAD},     {"reg", 0xAE},     {"macr", 0xAF},
    {"deg", 0xB0},     {"plusmn", 0xB1},  {"sup2", 0xB2},    {"sup3", 0xB3},    {"acute", 0xB4},   {"micro", 0xB5},   {"para", 0xB6},
    {"middot", 0xB7},  {"cedil", 0xB8},   {"sup1", 0xB9},    {"ordm", 0xBA},    {"raquo", 0xBB},   {"frac14", 0xBC},  {"frac12", 0xBD},
    {"frac34", 0xBE},  {"iquest", 0xBF},  {"Agrave", 0xC0},  {"Aacute", 0xC1},  {"Acirc", 0xC2},   {"Atilde", 0xC3},  {"Auml", 0xC4},
    {"Aring", 0xC5},   {"AElig", 0xC6},   {"Ccedil", 0xC7},  {"Egrave", 0xC8},  {"Eacute", 0xC9},  {"Ecirc", 0xCA},   {"Euml", 0xCB},
    {"Igrave", 0xCC},  {"Iacute", 0xCD},  {"Icirc", 0xCE},   {"Iuml", 0xCF},    {"ETH", 0xD0},     {"Ntilde", 0xD1},  {"Ograve", 0xD2},
    {"Oacute", 0xD3},  {"Ocirc", 0xD4},   {"Otilde", 0xD5},  {"Ouml", 0xD6},    {"times", 0xD7},   {"Oslash", 0xD8},  {"Ugrave", 0xD9},
    {"Uacute", 0xDA},  {"Ucirc", 0xDB},   {"Uuml", 0xDC},    {"Yacute", 0xDD},  {"THORN", 0xDE},   {"szlig", 0xDF},   {"agrave", 0xE0},
    {"aacute", 0xE1},  {"acirc", 0xE2},   {"atilde", 0xE3},  {"auml", 0xE4},    {"aring", 0xE5},   {"aelig", 0xE6},   {"ccedil", 0xE7},
    {"egrave", 0xE8},  {"eacute", 0xE9},  {"ecirc", 0xEA},   {"euml", 0xEB},    {"igrave", 0xEC},  {"iacute", 0xED},  {"icirc", 0xEE},
    {"iuml", 0xEF},    {"eth", 0xF0},     {"ntilde", 0xF1},  {"ograve", 0xF2},  {"oacute", 0xF3},  {"ocirc", 0xF4},   {"otilde", 0xF5},
    {"ouml", 0xF6},    {"divide", 0xF7},  {"oslash", 0xF8},  {"ugrave", 0xF9},  {"uacute", 0xFA},  {"ucirc", 0xFB},   {"uuml", 0xFC},
    {"yacute", 0xFD},  {"thorn", 0xFE},   {"yuml", 0xFF},    {"OElig", 0x152},  {"oelig", 0x153},  {"Scaron", 0x160}, {"scaron", 0x161},
    {"Yuml", 0x178},   {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C},
    {"rdquo", 0x201D}, {"bdquo", 0x201E}, {"dagger", 0x2020}, {"Dagger", 0x2021}, {"bull", 0x2022}, {"hellip", 0x2026}, {"euro", 0x20AC},
    {"hyphen", 0x2010}, {"thinsp", 0x2009}, {"ensp", 0x2002}, {"emsp", 0x2003}, {"zwj", 0x200D},  {"zwnj", 0x200C},
};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
bool nameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ':' || c == '.';
}

}  // namespace

void appendUtf8(std::string& out, unsigned cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

std::string decodeEntities(const std::string& s) {
  if (s.find('&') == std::string::npos) return s;
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') {
      out += s[i];
      continue;
    }
    const size_t semi = s.find(';', i + 1);
    if (semi == std::string::npos || semi - i > 10 || semi == i + 1) {
      out += '&';
      continue;
    }
    const std::string name = s.substr(i + 1, semi - i - 1);
    unsigned cp = 0;
    bool ok = false;
    if (name[0] == '#') {
      char* end = nullptr;
      const bool hex = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
      const unsigned long v = std::strtoul(name.c_str() + (hex ? 2 : 1), &end, hex ? 16 : 10);
      ok = end && *end == '\0' && end != name.c_str() + (hex ? 2 : 1) && v <= 0x10FFFF;
      cp = static_cast<unsigned>(v);
    } else {
      for (const Named& n : kNamed)
        if (name == n.name) {
          cp = n.cp;
          ok = true;
          break;
        }
    }
    if (ok) {
      appendUtf8(out, cp);
      i = semi;
    } else {
      out += '&';
    }
  }
  return out;
}

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
