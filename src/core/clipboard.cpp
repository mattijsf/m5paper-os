#include "core/clipboard.h"

#include "core/fs.h"
#include "core/log.h"

namespace pos::clipboard {

namespace {

const char* const kPath = "/data/clipboard.txt";

Sync& sync() {
  static Sync s;
  return s;
}

// Length of the valid UTF-8 sequence that starts at s[i], 0 if there is none (stray continuation byte, overlong form, surrogate,
// beyond U+10FFFF, cut off at the end).
size_t sequenceAt(const std::string& s, size_t i) {
  const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
  const unsigned char c = b(i);
  if (c < 0x80) return 1;
  size_t n = 0;
  unsigned cp = 0, min = 0;
  if ((c & 0xE0) == 0xC0) n = 2, cp = c & 0x1F, min = 0x80;
  else if ((c & 0xF0) == 0xE0) n = 3, cp = c & 0x0F, min = 0x800;
  else if ((c & 0xF8) == 0xF0) n = 4, cp = c & 0x07, min = 0x10000;
  else return 0;
  if (i + n > s.size()) return 0;
  for (size_t k = 1; k < n; ++k) {
    if ((b(i + k) & 0xC0) != 0x80) return 0;
    cp = (cp << 6) | (b(i + k) & 0x3F);
  }
  if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  return n;
}

// The file: "<source>\n<text>". Returns false if there is no clipboard.
bool load(std::string& src, std::string& text) {
  std::string raw;
  if (!fs::readFile(kPath, raw)) return false;
  const size_t nl = raw.find('\n');
  if (nl == std::string::npos) return false;
  src = raw.substr(0, nl);
  text = clean(raw.substr(nl + 1));  // the file may have been written by something else: same rules
  return !text.empty();
}

}  // namespace

std::string truncateUtf8(const std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t end = maxBytes;
  while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;  // s[end] starts a character: cut before it
  return s.substr(0, end);
}

std::string clean(const std::string& text, size_t maxBytes) {
  std::string out;
  out.reserve(text.size() < maxBytes ? text.size() : maxBytes);
  for (size_t i = 0; i < text.size();) {
    const size_t n = sequenceAt(text, i);
    if (n == 0) {  // not UTF-8: drop the byte
      ++i;
      continue;
    }
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (n == 1 && (c < 0x20 || c == 0x7F)) {
      if (c == '\n') out += '\n';
      else if (c == '\t') out += ' ';
      ++i;  // CR and the other control characters are dropped
      continue;
    }
    if (out.size() + n > maxBytes) break;  // a whole character or nothing
    out.append(text, i, n);
    i += n;
  }
  return truncateUtf8(out, maxBytes);
}

bool set(const std::string& text, const std::string& src) {
  const std::string t = clean(text);
  if (t.empty()) {
    clear();
    return true;
  }
  std::string who;
  for (char c : src)
    if (static_cast<unsigned char>(c) > 0x20 && c != 0x7F && who.size() < 32) who += c;
  const bool ok = fs::writeFile(kPath, who + "\n" + t);
  POS_LOG("clip", "%s: %u bytes from '%s'", ok ? "set" : "write failed", static_cast<unsigned>(t.size()), who.c_str());  // never the text
  if (ok && who != kFromPhone && syncing() && sync().push) sync().push(t);
  return ok;
}

void setSync(Sync s) { sync() = std::move(s); }

bool syncing() { return sync().active && sync().active(); }

void pull(std::function<void(bool)> done) {
  if (!syncing() || !sync().pull) {
    if (done) done(false);
    return;
  }
  sync().pull(std::move(done));
}

std::string get() {
  std::string src, text;
  return load(src, text) ? text : std::string();
}

std::string source() {
  std::string src, text;
  return load(src, text) ? src : std::string();
}

bool empty() { return get().empty(); }

void clear() { fs::removeAll(kPath); }

}  // namespace pos::clipboard
