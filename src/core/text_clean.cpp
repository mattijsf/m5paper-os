#include "core/text_clean.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace pos {

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


struct Emoji {
  unsigned cp;
  const char* text;
};
// The few that carry meaning in a short message: their text form when the panel has no emoji atlas (ui/text).
const Emoji kEmoji[] = {{0x1F600, ":)"}, {0x1F603, ":)"}, {0x1F604, ":)"}, {0x1F601, ":)"}, {0x1F642, ":)"}, {0x1F60A, ":)"}, {0x263A, ":)"},
                        {0x1F602, ":D"}, {0x1F923, ":D"}, {0x1F609, ";)"}, {0x1F61B, ":P"}, {0x1F622, ":("}, {0x1F62D, ":("}, {0x1F641, ":("},
                        {0x2639, ":("},  {0x1F620, ">:("}, {0x2764, "<3"},  {0x1F496, "<3"},  {0x1F44D, "(+1)"}, {0x1F44E, "(-1)"}, {0x1F64F, "(thanks)"}};

// Decodes one code point at s[i]; returns the bytes used (>= 1). cp = 0xFFFFFFFF if the bytes are not valid UTF-8.
size_t decodeAt(const std::string& s, size_t i, unsigned& cp) {
  const unsigned char b = static_cast<unsigned char>(s[i]);
  const size_t need = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : 99;
  if (need == 0) {
    cp = b;
    return 1;
  }
  if (need == 99 || i + need >= s.size()) {
    cp = 0xFFFFFFFFu;
    return 1;
  }
  cp = b & (0xFF >> (need + 2));
  for (size_t k = 1; k <= need; ++k) {
    const unsigned char n = static_cast<unsigned char>(s[i + k]);
    if ((n & 0xC0) != 0x80) {
      cp = 0xFFFFFFFFu;
      return 1;
    }
    cp = (cp << 6) | (n & 0x3F);
  }
  if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) || (need == 3 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFFFFFFu;
  return need + 1;
}

bool lowerEq(const std::string& s, size_t at, const char* what) {
  for (size_t i = 0; what[i]; ++i)
    if (at + i >= s.size() || std::tolower(static_cast<unsigned char>(s[at + i])) != what[i]) return false;
  return true;
}

// HTML to plain text: tags removed, <br> and block ends become line breaks, <style>/<script> content skipped, entities decoded.
std::string htmlToText(const std::string& h) {
  std::string out;
  size_t i = 0;
  const size_t n = h.size();
  auto newline = [&out] {
    if (!out.empty() && out.back() != '\n') out += '\n';
  };
  while (i < n) {
    if (h[i] != '<') {
      const size_t lt = h.find('<', i);
      const size_t end = lt == std::string::npos ? n : lt;
      out += decodeEntities(h.substr(i, end - i));
      i = end;
      continue;
    }
    if (h.compare(i, 4, "<!--") == 0) {
      const size_t e = h.find("-->", i + 4);
      i = e == std::string::npos ? n : e + 3;
      continue;
    }
    const size_t gt = h.find('>', i);
    const bool tagStart = i + 1 < n && (std::isalpha(static_cast<unsigned char>(h[i + 1])) || h[i + 1] == '/' || h[i + 1] == '!' || h[i + 1] == '?');
    if (!tagStart || gt == std::string::npos || gt - i > 2000) {  // not a tag: a stray '<'
      out += '<';
      ++i;
      continue;
    }
    const size_t nameAt = i + 1 + (i + 1 < n && h[i + 1] == '/' ? 1 : 0);
    if (lowerEq(h, nameAt, "style") || lowerEq(h, nameAt, "script") || lowerEq(h, nameAt, "head")) {
      const bool closing = h[i + 1] == '/';
      if (!closing) {
        const char* close = lowerEq(h, nameAt, "style") ? "</style" : lowerEq(h, nameAt, "script") ? "</script" : "</head";
        size_t e = i;
        while ((e = h.find("</", e + 1)) != std::string::npos && !lowerEq(h, e, close)) {}
        if (e == std::string::npos) {
          i = n;
          continue;
        }
        const size_t eg = h.find('>', e);
        i = eg == std::string::npos ? n : eg + 1;
        continue;
      }
    }
    static const char* kBlocks[] = {"br", "p", "div", "tr", "li", "h1", "h2", "h3", "h4", "h5", "h6", "blockquote", "table", "ul", "ol", "hr", "pre"};
    for (const char* b : kBlocks) {
      if (lowerEq(h, nameAt, b)) {
        const size_t after = nameAt + std::string(b).size();
        if (after < n && (h[after] == '>' || h[after] == ' ' || h[after] == '/' || std::isspace(static_cast<unsigned char>(h[after])))) {
          newline();
          if (std::string(b) == "p" && h[i + 1] == '/') out += '\n';  // a paragraph end leaves a blank line
          break;
        }
      }
    }
    if (lowerEq(h, nameAt, "td") || lowerEq(h, nameAt, "th")) out += ' ';
    i = gt + 1;
  }
  return out;
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

bool isEmojiRange(unsigned cp) {
  return (cp >= 0x2100 && cp <= 0x2BFF) || cp == 0x203C || cp == 0x2049 || cp == 0x3030 || cp == 0x303D || cp == 0x3297 ||
         cp == 0x3299 || (cp >= 0x1F000 && cp <= 0x1FAFF);
}

bool isEmojiModifier(unsigned cp) {
  return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) || cp == 0x20E3 || (cp >= 0xE0020 && cp <= 0xE007F);
}

const char* emojiFallback(unsigned cp) {
  for (const Emoji& e : kEmoji)
    if (e.cp == cp) return e.text;
  return nullptr;
}

bool hasGlyph(unsigned cp) { return (cp >= 0x20 && cp <= 0x7E) || (cp >= 0xA0 && cp <= 0x17F) || (cp >= 0x2010 && cp <= 0x2027) || cp == 0x20AC; }

std::string hostOfUrl(const std::string& url) {
  size_t s = 0;
  if (url.compare(0, 8, "https://") == 0) s = 8;
  else if (url.compare(0, 7, "http://") == 0) s = 7;
  else return "";
  size_t e = url.find_first_of("/?#", s);
  std::string host = url.substr(s, e == std::string::npos ? std::string::npos : e - s);
  const size_t at = host.rfind('@');
  if (at != std::string::npos) host.erase(0, at + 1);
  const size_t colon = host.find(':');
  if (colon != std::string::npos) host.erase(colon);
  if (host.compare(0, 4, "www.") == 0) host.erase(0, 4);
  return host;
}

std::string cleanText(const std::string& inRaw, const CleanOptions& opt) {
  const std::string in = opt.html ? htmlToText(inRaw) : inRaw;
  // 1. characters: valid, printable, with a glyph (or a text form)
  std::string chars;
  chars.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    unsigned cp;
    const size_t n = decodeAt(in, i, cp);
    i += n;
    if (cp == 0xFFFFFFFFu) continue;
    if (cp == '\r') {
      if (i < in.size() && in[i] == '\n') ++i;
      cp = '\n';
    }
    if (cp == '\n') {
      chars += '\n';
      continue;
    }
    if (cp == '\t' || cp == 0xA0 || cp == 0x2009 || cp == 0x2002 || cp == 0x2003 || cp == 0x202F) {
      chars += ' ';
      continue;
    }
    if (cp == 0x200D || isEmojiModifier(cp)) {  // sequences stay whole: ui/text draws 👩‍💻, 🇳🇱, keycaps from the atlas
      appendUtf8(chars, cp);
      continue;
    }
    if (cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0) || cp == 0x200B || cp == 0x200C || cp == 0xFEFF) continue;
    if (cp == 0xAD) continue;  // soft hyphen
    if (hasGlyph(cp)) {
      appendUtf8(chars, cp);
      continue;
    }
    if (isEmojiRange(cp)) appendUtf8(chars, cp);  // drawn from the emoji atlas, or as emojiFallback() / dropped without one
  }
  // 2. links to host names; long words cut
  std::string words;
  words.reserve(chars.size());
  size_t i = 0;
  while (i < chars.size()) {
    if (chars[i] == ' ' || chars[i] == '\n') {
      words += chars[i++];
      continue;
    }
    size_t e = i;
    while (e < chars.size() && chars[e] != ' ' && chars[e] != '\n') ++e;
    std::string w = chars.substr(i, e - i);
    i = e;
    size_t linkAt = std::string::npos;
    if (opt.shortenLinks) {
      const size_t a = w.find("http://"), b = w.find("https://");
      linkAt = std::min(a, b);
    }
    if (linkAt != std::string::npos) {
      // what comes before the link ("(") and after it (".", ")") stays outside
      const std::string head = w.substr(0, linkAt);
      w = w.substr(linkAt);
      std::string tail;
      while (!w.empty() && std::string(".,;:!?)\"'").find(w.back()) != std::string::npos) {
        tail.insert(tail.begin(), w.back());
        w.pop_back();
      }
      const std::string host = hostOfUrl(w);
      w = head + (host.empty() ? w : host) + tail;
    }
    if (opt.maxWord > 3 && w.size() > opt.maxWord) {
      size_t cut = opt.maxWord - 1;
      while (cut > 0 && (static_cast<unsigned char>(w[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
      w = w.substr(0, cut) + "\xE2\x80\xA6";
    }
    words += w;
  }
  // 3. blanks: runs of spaces to one, spaces at line ends dropped, at most one blank line, trimmed
  std::string out;
  out.reserve(words.size());
  int newlines = 0;
  bool pendingSpace = false;
  for (char c : words) {
    if (c == ' ') {
      pendingSpace = !out.empty() && out.back() != '\n';
      continue;
    }
    if (c == '\n') {
      pendingSpace = false;
      if (out.empty()) continue;
      if (++newlines <= 2) out += '\n';
      continue;
    }
    if (pendingSpace) out += ' ';
    pendingSpace = false;
    newlines = 0;
    out += c;
  }
  while (!out.empty() && out.back() == '\n') out.pop_back();
  return out;
}

}  // namespace pos
