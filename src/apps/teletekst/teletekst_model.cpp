#include "apps/teletekst/teletekst_model.h"

#include <ArduinoJson.h>

#include <cstdlib>
#include <cstring>

#include "core/utf8.h"

namespace pos::teletekst {

namespace {

struct Style {
  uint8_t fg = White, bg = Black;
  uint16_t link = 0;
  uint8_t linkSub = 0;
};

bool colourOf(const std::string& name, uint8_t& out) {
  static const struct {
    const char* name;
    Colour colour;
  } kColours[] = {{"black", Black}, {"red", Red},         {"green", Green}, {"yellow", Yellow},
                  {"blue", Blue},   {"magenta", Magenta}, {"cyan", Cyan},   {"white", White}};
  for (const auto& c : kColours) {
    if (name == c.name) {
      out = c.colour;
      return true;
    }
  }
  return false;
}

// The value of attribute `name` in the inside of a tag (`a class="yellow" href="#104"`), "" if it has none.
std::string attribute(const std::string& tag, const char* name) {
  const std::string key = std::string(name) + "=\"";
  size_t at = tag.find(key);
  if (at == std::string::npos) return "";
  at += key.size();
  const size_t end = tag.find('"', at);
  return end == std::string::npos ? "" : tag.substr(at, end - at);
}

// Named character references a Dutch page can hold. Anything else shows as '?'.
uint32_t namedEntity(const std::string& name) {
  static const struct {
    const char* name;
    uint16_t cp;
  } kNames[] = {
      {"amp", '&'},      {"lt", '<'},        {"gt", '>'},        {"quot", '"'},      {"apos", '\''},     {"nbsp", ' '},
      {"euro", 0x20AC},  {"pound", 0xA3},    {"sect", 0xA7},     {"copy", 0xA9},     {"laquo", 0xAB},    {"reg", 0xAE},
      {"deg", 0xB0},     {"plusmn", 0xB1},   {"sup2", 0xB2},     {"sup3", 0xB3},     {"micro", 0xB5},    {"middot", 0xB7},
      {"raquo", 0xBB},   {"frac14", 0xBC},   {"frac12", 0xBD},   {"frac34", 0xBE},   {"iquest", 0xBF},   {"iexcl", 0xA1},
      {"times", 0xD7},   {"divide", 0xF7},   {"szlig", 0xDF},    {"ndash", 0x2013},  {"mdash", 0x2014},  {"lsquo", 0x2018},
      {"rsquo", 0x2019}, {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"bull", 0x2022},   {"hellip", 0x2026},
      {"Agrave", 0xC0},  {"Aacute", 0xC1},   {"Acirc", 0xC2},    {"Atilde", 0xC3},   {"Auml", 0xC4},     {"Aring", 0xC5},
      {"AElig", 0xC6},   {"Ccedil", 0xC7},   {"Egrave", 0xC8},   {"Eacute", 0xC9},   {"Ecirc", 0xCA},    {"Euml", 0xCB},
      {"Igrave", 0xCC},  {"Iacute", 0xCD},   {"Icirc", 0xCE},    {"Iuml", 0xCF},     {"Ntilde", 0xD1},   {"Ograve", 0xD2},
      {"Oacute", 0xD3},  {"Ocirc", 0xD4},    {"Otilde", 0xD5},   {"Ouml", 0xD6},     {"Oslash", 0xD8},   {"Ugrave", 0xD9},
      {"Uacute", 0xDA},  {"Ucirc", 0xDB},    {"Uuml", 0xDC},     {"Yacute", 0xDD},   {"agrave", 0xE0},   {"aacute", 0xE1},
      {"acirc", 0xE2},   {"atilde", 0xE3},   {"auml", 0xE4},     {"aring", 0xE5},    {"aelig", 0xE6},    {"ccedil", 0xE7},
      {"egrave", 0xE8},  {"eacute", 0xE9},   {"ecirc", 0xEA},    {"euml", 0xEB},     {"igrave", 0xEC},   {"iacute", 0xED},
      {"icirc", 0xEE},   {"iuml", 0xEF},     {"ntilde", 0xF1},   {"ograve", 0xF2},   {"oacute", 0xF3},   {"ocirc", 0xF4},
      {"otilde", 0xF5},  {"ouml", 0xF6},     {"oslash", 0xF8},   {"ugrave", 0xF9},   {"uacute", 0xFA},   {"ucirc", 0xFB},
      {"uuml", 0xFC},    {"yacute", 0xFD},   {"yuml", 0xFF},
  };
  for (const auto& n : kNames)
    if (name == n.name) return n.cp;
  return '?';
}

// "&#xF02c;", "&#233;", "&eacute;" without the & and the ; .
uint32_t entity(const std::string& body) {
  if (body.size() >= 2 && body[0] == '#') {
    const bool hex = body[1] == 'x' || body[1] == 'X';
    char* end = nullptr;
    const char* begin = body.c_str() + (hex ? 2 : 1);
    const long v = std::strtol(begin, &end, hex ? 16 : 10);
    if (end == begin || *end != '\0' || v <= 0 || v > 0x10FFFF) return '?';
    return static_cast<uint32_t>(v);
  }
  return namedEntity(body);
}

void put(Page& page, int row, int col, uint32_t cp, const Style& st) {
  if (row < 0 || row >= kRows || col < 0 || col >= kCols) return;
  Cell& c = page.cells[row][col];
  c.fg = st.fg;
  c.bg = st.bg;
  c.link = st.link;
  c.linkSub = st.linkSub;
  c.mosaic = false;
  if (cp >= 0xF000 && cp <= 0xF0FF) {
    // The site's private-use characters are the mosaic set of teletext: code 0x20..0x3F and 0x60..0x7F, a bit per block.
    const uint32_t code = cp & 0x7F;
    if (code >= 0x40 && code < 0x60) {  // (never seen) the capitals that sit inside the mosaic set
      c.ch = static_cast<uint16_t>(code);
    } else {
      c.mosaic = true;
      c.ch = static_cast<uint16_t>((code & 0x1F) | ((code & 0x40) >> 1));
    }
    return;
  }
  if (cp < 0x20 || cp > 0xFFFF || cp == 0xA0) cp = cp == 0xA0 ? ' ' : '?';
  c.ch = static_cast<uint16_t>(cp);
}

}  // namespace

std::string PageRef::id() const {
  std::string s = std::to_string(page);
  if (sub > 0) s += "-" + std::to_string(sub);
  return s;
}

bool parseRef(const std::string& text, PageRef& out) {
  size_t i = !text.empty() && text[0] == '#' ? 1 : 0;
  auto digit = [&](size_t at) { return at < text.size() && text[at] >= '0' && text[at] <= '9'; };
  if (!digit(i) || !digit(i + 1) || !digit(i + 2)) return false;
  PageRef r;
  r.page = (text[i] - '0') * 100 + (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
  i += 3;
  if (i < text.size()) {
    if (text[i] != '-' && text[i] != '/') return false;
    ++i;
    if (!digit(i)) return false;
    while (digit(i)) {
      r.sub = r.sub * 10 + (text[i] - '0');
      if (r.sub > 99) return false;
      ++i;
    }
    if (i != text.size()) return false;
  }
  if (!r.valid()) return false;
  out = r;
  return true;
}

std::string pageUrl(const PageRef& ref) { return std::string(kBaseUrl) + ref.id(); }

void parseContent(const std::string& html, Page& out) {
  for (auto& row : out.cells)
    for (Cell& c : row) c = Cell{};
  std::vector<Style> stack;
  Style st;
  int row = 0, col = 0;
  size_t i = 0;
  while (i < html.size() && row < kRows) {
    const char ch = html[i];
    if (ch == '\n') {
      ++row;
      col = 0;
      ++i;
    } else if (ch == '\r') {
      ++i;
    } else if (ch == '<') {
      const size_t end = html.find('>', i);
      if (end == std::string::npos) break;
      const std::string tag = html.substr(i + 1, end - i - 1);
      i = end + 1;
      if (!tag.empty() && tag[0] == '/') {
        if (!stack.empty()) {
          st = stack.back();
          stack.pop_back();
        }
        continue;
      }
      stack.push_back(st);
      // class="yellow bg-blue ": a text colour, a background, or both
      const std::string classes = attribute(tag, "class");
      size_t a = 0;
      while (a < classes.size()) {
        size_t b = classes.find(' ', a);
        if (b == std::string::npos) b = classes.size();
        const std::string word = classes.substr(a, b - a);
        if (word.compare(0, 3, "bg-") == 0) {
          colourOf(word.substr(3), st.bg);
        } else {
          colourOf(word, st.fg);
        }
        a = b + 1;
      }
      if (tag.compare(0, 2, "a ") == 0) {
        PageRef to;
        if (parseRef(attribute(tag, "href"), to)) {
          st.link = static_cast<uint16_t>(to.page);
          st.linkSub = static_cast<uint8_t>(to.sub);
        }
      }
    } else if (ch == '&') {
      const size_t end = html.find(';', i);
      if (end == std::string::npos || end - i > 10) {  // a bare ampersand
        put(out, row, col++, '&', st);
        ++i;
      } else {
        put(out, row, col++, entity(html.substr(i + 1, end - i - 1)), st);
        i = end + 1;
      }
    } else {
      uint32_t cp = 0;
      i += utf8::decode(html, i, cp);
      put(out, row, col++, cp == utf8::kInvalid ? '?' : cp, st);
    }
  }
}

bool parsePage(const std::string& json, const PageRef& asked, Page& out) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["content"].is<const char*>()) return false;
  const std::string content = doc["content"].as<const char*>();
  if (content.empty()) return false;
  // Field by field: a Page is 8 KB and the UI thread's stack on the device is 8 KB, so no temporary Page{} here.
  out.self = asked;
  out.fast.clear();
  auto ref = [&doc](const char* key) {
    PageRef r;
    if (doc[key].is<const char*>()) parseRef(doc[key].as<const char*>(), r);
    return r;
  };
  out.prev = ref("prevPage");
  out.next = ref("nextPage");
  out.prevSub = ref("prevSubPage");
  out.nextSub = ref("nextSubPage");
  if (out.hasSubpages() && out.self.sub == 0) out.self.sub = 1;
  for (JsonObjectConst link : doc["fastTextLinks"].as<JsonArrayConst>()) {
    FastLink f;
    if (!link["title"].is<const char*>() || !link["page"].is<const char*>()) continue;
    f.title = link["title"].as<const char*>();
    if (!parseRef(link["page"].as<const char*>(), f.to) || out.fast.size() >= 4) continue;
    out.fast.push_back(f);
  }
  parseContent(content, out);
  return true;
}

PageRef linkNear(const Page& page, int row, int col) {
  if (row < 0 || row >= kRows) return PageRef{};
  PageRef best;
  int bestDistance = 1000;
  auto scan = [&](int r, int penalty) {
    if (r < 0 || r >= kRows) return;
    for (int c = 0; c < kCols; ++c) {
      const Cell& cell = page.cells[r][c];
      if (cell.link == 0) continue;
      const int d = std::abs(c - col) + penalty;
      if (d < bestDistance) {
        bestDistance = d;
        best.page = cell.link;
        best.sub = cell.linkSub;
      }
    }
  };
  scan(row, 0);
  if (best.valid()) return best;
  scan(row - 1, 0);
  scan(row + 1, 0);
  return best;
}

std::string rowText(const Page& page, int row) {
  std::string s;
  if (row < 0 || row >= kRows) return s;
  for (const Cell& c : page.cells[row]) {
    if (c.mosaic) {
      s += ' ';
    } else {
      utf8::append(s, c.ch);
    }
  }
  return s;
}

std::string packSaved(int64_t fetched, const std::string& json) { return std::to_string(fetched) + "\n" + json; }

bool unpackSaved(const std::string& saved, int64_t& fetched, std::string& json) {
  const size_t nl = saved.find('\n');
  if (nl == std::string::npos || nl == 0 || nl > 20) return false;
  char* end = nullptr;
  const std::string first = saved.substr(0, nl);
  const long long t = std::strtoll(first.c_str(), &end, 10);
  if (end == first.c_str() || *end != '\0') return false;
  fetched = t;
  json = saved.substr(nl + 1);
  return !json.empty();
}

}  // namespace pos::teletekst
