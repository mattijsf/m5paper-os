#include "core/utf8.h"

namespace pos::utf8 {

size_t decode(const std::string& s, size_t i, uint32_t& cp) {
  const unsigned char b = static_cast<unsigned char>(s[i]);
  if (b < 0x80) {
    cp = b;
    return 1;
  }
  cp = kInvalid;
  const size_t need = (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : 0;  // continuation bytes
  if (need == 0 || i + need >= s.size()) return 1;
  uint32_t v = b & (0xFFu >> (need + 2));
  for (size_t k = 1; k <= need; ++k) {
    const char n = s[i + k];
    if (!isContinuation(n)) return 1;
    v = (v << 6) | (static_cast<unsigned char>(n) & 0x3F);
  }
  static const uint32_t kMin[] = {0, 0x80, 0x800, 0x10000};  // below this the form is overlong
  if (v < kMin[need] || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 1;
  cp = v;
  return need + 1;
}

void append(std::string& out, uint32_t cp) {
  if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
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

bool valid(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t cp;
    i += decode(s, i, cp);
    if (cp == kInvalid) return false;
  }
  return true;
}

size_t length(const std::string& s) {
  size_t n = 0;
  for (char c : s) n += !isContinuation(c);
  return n;
}

std::string prefix(const std::string& s, size_t chars) {
  size_t i = 0;
  for (; i < s.size(); ++i) {
    if (isContinuation(s[i])) continue;
    if (chars == 0) break;
    --chars;
  }
  return s.substr(0, i);
}

std::string truncate(const std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t end = maxBytes;
  while (end > 0 && isContinuation(s[end])) --end;  // s[end] starts a character: cut before it
  return s.substr(0, end);
}

size_t lastStart(const std::string& s) {
  if (s.empty()) return 0;
  size_t i = s.size() - 1;
  while (i > 0 && isContinuation(s[i])) --i;
  return i;
}

size_t span(const std::string& s, size_t i) {
  size_t n = 1;
  while (i + n < s.size() && isContinuation(s[i + n])) ++n;
  return n;
}

}  // namespace pos::utf8
