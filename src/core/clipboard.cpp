#include "core/clipboard.h"

#include "core/fs.h"
#include "core/log.h"
#include "core/utf8.h"

namespace pos::clipboard {

namespace {

const char* const kPath = "/data/clipboard.txt";
const char* const kUnsentPath = "/data/clipboard_unsent";  // the time of a copy the phone has not got: one decimal number

Sync& sync() {
  static Sync s;
  return s;
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

std::string clean(const std::string& text, size_t maxBytes) {
  std::string out;
  out.reserve(text.size() < maxBytes ? text.size() : maxBytes);
  for (size_t i = 0; i < text.size();) {
    uint32_t cp;
    const size_t n = utf8::decode(text, i, cp);
    if (cp == utf8::kInvalid) {  // not UTF-8: drop the byte
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
  return out;
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
  if (ok) fs::removeAll(kUnsentPath);  // an older copy is no longer owed: this one is pushed now (or came from the phone)
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

void clear() {
  fs::removeAll(kPath);
  fs::removeAll(kUnsentPath);
}

namespace {
bool unsentTime(int64_t& ts) {
  std::string s;
  if (!fs::readFile(kUnsentPath, s) || s.empty() || s.size() > 20) return false;
  int64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  ts = v;
  return true;
}
}  // namespace

void markUnsent(int64_t ts) { fs::writeFile(kUnsentPath, std::to_string(ts < 0 ? 0 : ts)); }

void markSent(int64_t ts) {
  int64_t pending = 0;
  if (unsentTime(pending) && pending > ts) return;  // a later copy failed too: that one is still owed
  fs::removeAll(kUnsentPath);
}

bool unsent(int64_t now, int64_t maxAge, std::string& text, int64_t& ts) {
  if (!unsentTime(ts)) {
    fs::removeAll(kUnsentPath);  // missing or damaged
    return false;
  }
  std::string src;
  if (!load(src, text) || text.empty() || src == kFromPhone || now - ts > maxAge) {
    fs::removeAll(kUnsentPath);
    return false;
  }
  return true;
}

}  // namespace pos::clipboard
