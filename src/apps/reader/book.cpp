#include "apps/reader/book.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <ArduinoJson.h>

#include "apps/reader/epub.h"
#include "apps/reader/xhtml.h"
#include "core/fs.h"
#include "core/zip.h"

namespace pos::reader {

Timing& timing() {
  static Timing t;
  return t;
}

namespace {

using TimerClock = std::chrono::steady_clock;
uint32_t since(TimerClock::time_point t) { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(TimerClock::now() - t).count()); }

// The worker runs long CPU-bound jobs at the lowest priority on its own core (platform/device/main.cpp); sleeping a tick every
// 40 ms lets the idle task run (its watchdog resets the board otherwise) and keeps USB and Wi-Fi serviced.
void cooperate() {
  using clock = std::chrono::steady_clock;
  thread_local clock::time_point last = clock::now();
  const auto now = clock::now();
  if (now - last > std::chrono::milliseconds(40)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    last = clock::now();
  }
}

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

bool endsWith(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool validUtf8(const std::string& s) {
  size_t i = 0;
  while (i < s.size()) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    const size_t need = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : 99;
    if (need == 99 || i + need >= s.size() + (need == 0 ? 1 : 0)) return false;
    for (size_t k = 1; k <= need; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    i += need + 1;
  }
  return true;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) out += static_cast<char>(cp);
  else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

// Windows-1252 for 0x80..0x9F, identical to Latin-1 elsewhere.
const uint16_t kCp1252[32] = {0x20AC, 0x81,   0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                              0x2039, 0x0152, 0x8D,   0x017D, 0x8F,   0x90,   0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                              0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D,   0x017E, 0x0178};

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
  return s.substr(a, b - a);
}

std::string headerValue(const std::string& text, const char* key) {
  const std::string k = std::string("\n") + key + ": ";
  size_t p = text.find(k);
  if (p == std::string::npos || p > 8000) return "";
  p += k.size();
  size_t e = text.find('\n', p);
  return trim(text.substr(p, e == std::string::npos ? std::string::npos : e - p));
}

// _word_ and _two words_ -> italic marks. Only pairs inside one paragraph; an odd underscore stays a character.
std::string italicsFromUnderscores(const std::string& s) {
  size_t first = s.find('_');
  if (first == std::string::npos) return s;
  std::string out;
  bool open = false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '_') {
      out += s[i];
      continue;
    }
    const bool prevWord = i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1]));
    const bool nextWord = i + 1 < s.size() && std::isalnum(static_cast<unsigned char>(s[i + 1]));
    if (!open && !prevWord && s.find('_', i + 1) != std::string::npos) {
      out += '\x03';
      open = true;
    } else if (open && !nextWord) {
      out += '\x04';
      open = false;
    } else {
      out += '_';
    }
  }
  if (open) out += '\x04';
  return out;
}

bool looksLikeHeading(const std::string& line) {
  if (line.empty() || line.size() > 60) return false;
  static const char* kWords[] = {"CHAPTER ", "Chapter ", "BOOK ", "Book ", "PART ", "Part ", "STAVE ", "Stave ", "LETTER ", "Letter ", "PROLOGUE", "EPILOGUE", "Epilogue", "Prologue"};
  for (const char* w : kWords)
    if (line.rfind(w, 0) == 0) return true;
  return false;
}

}  // namespace

bool isEpubFile(const std::string& name) { return !name.empty() && name[0] != '.' && endsWith(lower(name), ".epub"); }
bool isBookFile(const std::string& name) { return !name.empty() && name[0] != '.' && (endsWith(lower(name), ".epub") || endsWith(lower(name), ".txt")); }

// The name without its folder: a book moved into a folder (Files over Wi-Fi) keeps its id, so its cache and reading position.
static std::string baseName(const std::string& path) { return path.substr(path.rfind('/') == std::string::npos ? 0 : path.rfind('/') + 1); }

std::string bookId(const std::string& fileName, uint64_t size, int64_t mtime) {
  const std::string key = baseName(fileName) + "|" + std::to_string(size) + "|" + std::to_string(mtime);
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : key) {
    h ^= c;
    h *= 1099511628211ull;
  }
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
  return std::string(buf).substr(0, 12);
}

std::string cacheDir(const std::string& id) { return std::string(kCacheRoot) + "/" + id; }

void titleFromFileName(const std::string& file, std::string& title, std::string& author) {
  std::string base = file;
  const size_t dot = base.rfind('.');
  if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
  const size_t dash = base.rfind(" - ");
  if (dash != std::string::npos && dash > 0) {
    title = trim(base.substr(0, dash));
    author = trim(base.substr(dash + 3));
  } else {
    title = trim(base);
    author.clear();
  }
}

// ---- TXT

std::string txtToUtf8(const std::string& rawIn) {
  std::string raw = rawIn;
  if (raw.size() >= 3 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
  std::string out;
  out.reserve(raw.size());
  if (validUtf8(raw)) {
    out = raw;
  } else {
    for (unsigned char c : raw) {
      if (c < 0x80) out += static_cast<char>(c);
      else if (c >= 0x80 && c < 0xA0) appendUtf8(out, kCp1252[c - 0x80]);
      else appendUtf8(out, c);
    }
  }
  // CRLF and lone CR -> LF
  std::string lf;
  lf.reserve(out.size());
  for (size_t i = 0; i < out.size(); ++i) {
    if (out[i] == '\r') {
      lf += '\n';
      if (i + 1 < out.size() && out[i + 1] == '\n') ++i;
    } else {
      lf += out[i];
    }
  }
  return lf;
}

TxtImport importTxt(const std::string& textIn, size_t maxChapterBytes) {
  TxtImport r;
  std::string text = "\n" + textIn;
  r.title = headerValue(text, "Title");
  r.author = headerValue(text, "Author");
  // Project Gutenberg: keep what is between the START and END marker lines.
  size_t start = text.find("*** START OF");
  if (start != std::string::npos) {
    size_t nl = text.find('\n', start);
    if (nl != std::string::npos) text.erase(0, nl + 1);
    size_t end = text.find("*** END OF");
    if (end != std::string::npos) text.erase(end);
  }

  std::vector<std::string> paras;  // marked text, one per paragraph
  std::string cur;
  int curLines = 0;
  auto flush = [&] {
    if (cur.empty()) return;
    std::string p = cur;
    if (curLines == 1 && looksLikeHeading(p)) p = std::string("\x05") + "2" + p;
    else p = italicsFromUnderscores(p);
    paras.push_back(std::move(p));
    cur.clear();
    curLines = 0;
  };
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(pos, e - pos);
    pos = e + 1;
    // Control bytes would be read as style marks later; tabs are spaces.
    std::string clean;
    for (char c : line) {
      const unsigned char u = static_cast<unsigned char>(c);
      if (c == '\t') clean += ' ';
      else if (u >= 0x20 || u >= 0x80) clean += c;
    }
    line = trim(clean);
    if (line.empty()) {
      flush();
    } else {
      std::string squeezed;  // runs of blanks inside the line become one space
      for (char c : line)
        if (c != ' ' || squeezed.empty() || squeezed.back() != ' ') squeezed += c;
      if (!cur.empty()) cur += ' ';
      cur += squeezed;
      ++curLines;
    }
    if (e >= text.size()) break;
  }
  flush();

  std::string chapter;
  for (const std::string& p : paras) {
    if (!chapter.empty() && chapter.size() + p.size() > maxChapterBytes) {
      r.chapters.push_back(std::move(chapter));
      chapter.clear();
    }
    chapter += p;
    chapter += '\n';
  }
  if (!chapter.empty() || r.chapters.empty()) r.chapters.push_back(std::move(chapter));
  return r;
}

// ---- EPUB

bool epubMeta(const std::string& virtualPath, BookMeta& meta, std::string& error) {
  zip::Archive z;
  if (!z.open(virtualPath)) {
    error = "Not a valid EPUB file (" + z.error() + ")";
    return false;
  }
  EpubInfo info;
  if (!parseEpub(z, info, error)) return false;
  meta.epub = true;
  if (!info.title.empty()) meta.title = info.title;
  if (!info.author.empty()) meta.author = info.author;
  meta.cover = info.cover;
  meta.tocEntries = info.toc;
  meta.chapters.clear();
  for (size_t i = 0; i < info.chapters.size(); ++i)
    meta.chapters.push_back(Chapter{info.chapters[i].title.empty() ? "Chapter " + std::to_string(i + 1) : info.chapters[i].title, info.chapters[i].href,
                                    !info.chapters[i].title.empty(), 0, 0});
  return true;
}

namespace {

constexpr size_t kMaxChapterBytes = 512 * 1024;

// Where a long text is cut into parts: at a paragraph boundary at most `maxBytes` after the previous cut.
std::vector<std::pair<size_t, size_t>> splitRanges(const std::string& marked, size_t maxBytes) {
  std::vector<std::pair<size_t, size_t>> out;
  size_t pos = 0;
  while (pos < marked.size()) {
    size_t end = std::min(marked.size(), pos + maxBytes);
    if (end < marked.size()) {
      const size_t nl = marked.rfind('\n', end);
      end = (nl != std::string::npos && nl > pos) ? nl + 1 : end;
    }
    out.emplace_back(pos, end);
    pos = end;
  }
  return out;
}

}  // namespace

bool convertEpub(const std::string& virtualPath, BookMeta& meta, std::vector<std::string>& texts, std::string& error,
                 const std::function<bool(int, int)>& step) {
  zip::Archive z;
  if (!z.open(virtualPath)) {
    error = "Not a valid EPUB file (" + z.error() + ")";
    return false;
  }
  texts.clear();
  std::vector<Chapter> kept;
  const int n = static_cast<int>(meta.chapters.size());
  for (int i = 0; i < n; ++i) {
    cooperate();
    if (step && !step(i, n)) {
      error = "Cancelled";
      return false;
    }
    const Chapter& src = meta.chapters[i];
    std::string xhtml;
    auto tz = TimerClock::now();
    const bool readOk = z.read(src.href, xhtml, 8u * 1024 * 1024);
    timing().zipMs += since(tz);
    if (!readOk) continue;  // an unreadable chapter is skipped, the rest of the book stays readable
    auto tc = TimerClock::now();
    XhtmlResult r = xhtmlToMarked(xhtml);
    timing().convertMs += since(tc);
    xhtml.clear();
    if (r.marked.empty()) continue;

    // Where the contents cut this document: its entries' anchors, in text order.
    struct Cut {
      size_t at;
      std::string title;
    };
    std::vector<Cut> cuts;
    for (const TocEntry& t : meta.tocEntries) {
      if (t.path != src.href) continue;
      size_t at = 0;
      if (!t.fragment.empty()) {
        bool found = false;
        for (const auto& a : r.anchors)
          if (a.first == t.fragment) {
            at = a.second;
            found = true;
            break;
          }
        if (!found) continue;
      }
      cuts.push_back({at, t.title});
    }
    std::stable_sort(cuts.begin(), cuts.end(), [](const Cut& a, const Cut& b) { return a.at < b.at; });
    std::vector<Cut> uniq;  // one chapter per place
    for (const Cut& c : cuts)
      if (uniq.empty() || c.at != uniq.back().at) uniq.push_back(c);

    struct Section {
      size_t from, to;
      std::string title;
      bool toc;
    };
    std::vector<Section> sections;
    if (uniq.size() < 2) {
      std::string title = src.title;
      if (title == "Chapter " + std::to_string(i + 1) && !r.firstHeading.empty()) title = r.firstHeading;  // no contents title: for the list only
      sections.push_back({0, r.marked.size(), title, src.toc});
    } else {
      if (uniq[0].at > 0) sections.push_back({0, uniq[0].at, "Beginning", false});  // text before the first entry
      for (size_t k = 0; k < uniq.size(); ++k) sections.push_back({uniq[k].at, k + 1 < uniq.size() ? uniq[k + 1].at : r.marked.size(), uniq[k].title, true});
    }
    for (const Section& sec : sections) {
      bool hasText = false;
      for (size_t k = sec.from; k < sec.to && !hasText; ++k) hasText = !std::isspace(static_cast<unsigned char>(r.marked[k]));
      if (!hasText) continue;
      int part = 0;
      for (const auto& range : splitRanges(r.marked.substr(sec.from, sec.to - sec.from), kMaxChapterBytes)) {
        const size_t from = sec.from + range.first, to = sec.from + range.second;
        texts.push_back(r.marked.substr(from, to - from));
        kept.push_back(Chapter{part == 0 ? sec.title : sec.title + " (" + std::to_string(part + 1) + ")", src.href, sec.toc, from, to});
        ++part;
      }
    }
  }
  if (kept.empty()) {
    error = "This book has no text I can show";
    return false;
  }
  meta.chapters = std::move(kept);
  meta.tocEntries.clear();
  return true;
}

bool ChapterLoader::load(int n, std::string& marked) {
  marked.clear();
  if (n < 0 || n >= static_cast<int>(meta_.chapters.size())) return false;
  if (!meta_.epub) return readChapter(meta_.id, n, marked);
  const Chapter& ch = meta_.chapters[n];
  if (docHref_ != ch.href) {
    if (!zipTried_) {
      zipTried_ = true;
      zipOk_ = zip_.open(std::string(kBooksDir) + "/" + meta_.file);
    }
    std::string xhtml;
    if (!zipOk_ || !zip_.read(ch.href, xhtml, 8u * 1024 * 1024)) return false;
    doc_ = xhtmlToMarked(xhtml).marked;
    docHref_ = ch.href;
  }
  const size_t to = ch.to == 0 ? doc_.size() : std::min(ch.to, doc_.size());
  if (ch.from >= to) return false;
  marked = doc_.substr(ch.from, to - ch.from);
  return true;
}

namespace {
const char* coverExt(const std::string& zipPath) {
  const std::string l = lower(zipPath);
  if (endsWith(l, ".png")) return "png";
  if (endsWith(l, ".jpg") || endsWith(l, ".jpeg")) return "jpg";
  return nullptr;
}
}  // namespace

std::string coverFile(const BookMeta& meta) {
  const char* ext = meta.cover.empty() ? nullptr : coverExt(meta.cover);
  if (!ext) return "";
  const std::string path = cacheDir(meta.id) + "/cover." + ext;
  return fs::exists(path) ? path : "";
}

bool extractCover(const BookMeta& meta) {
  const char* ext = meta.cover.empty() ? nullptr : coverExt(meta.cover);
  if (!ext) return false;
  const std::string out = cacheDir(meta.id) + "/cover." + ext;
  if (fs::exists(out)) return true;
  zip::Archive z;
  if (!z.open(std::string(kBooksDir) + "/" + meta.file)) return false;
  const zip::Entry* e = z.find(meta.cover);
  if (!e) return false;
  fs::Writer w;
  bool ok = w.open(out + ".part");
  ok = ok && z.extract(*e, [&w](const uint8_t* d, size_t len) { return w.write(d, len); }, 4u * 1024 * 1024);
  ok = w.close() && ok;
  if (!ok || !fs::rename(out + ".part", out)) {
    fs::removeAll(out + ".part");
    return false;
  }
  return true;
}

// ---- cache

bool writeMeta(const BookMeta& m) {
  JsonDocument doc;
  doc["file"] = m.file;
  doc["title"] = m.title;
  doc["author"] = m.author;
  doc["epub"] = m.epub;
  if (!m.cover.empty()) doc["cover"] = m.cover;
  JsonArray ch = doc["chapters"].to<JsonArray>();
  for (const Chapter& c : m.chapters) {
    JsonObject o = ch.add<JsonObject>();
    o["title"] = c.title;
    if (!c.href.empty()) o["href"] = c.href;
    if (c.toc) o["toc"] = true;
    if (c.to) {
      o["from"] = static_cast<uint32_t>(c.from);
      o["to"] = static_cast<uint32_t>(c.to);
    }
  }
  std::string out;
  serializeJson(doc, out);
  return fs::writeFile(cacheDir(m.id) + "/meta.json", out);
}

bool readMeta(const std::string& id, BookMeta& m) {
  std::string data;
  if (!fs::readFile(cacheDir(id) + "/meta.json", data)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, data)) return false;
  m = BookMeta{};
  m.id = id;
  m.file = doc["file"] | "";
  m.title = doc["title"] | "";
  m.author = doc["author"] | "";
  m.epub = doc["epub"] | false;
  m.cover = doc["cover"] | "";
  for (JsonVariant v : doc["chapters"].as<JsonArray>()) m.chapters.push_back(Chapter{v["title"] | "", v["href"] | "", v["toc"] | false, v["from"] | static_cast<uint32_t>(0), v["to"] | static_cast<uint32_t>(0)});
  return !m.chapters.empty();
}

bool writeChapter(const std::string& id, int n, const std::string& marked) {
  return fs::writeFile(cacheDir(id) + "/ch" + std::to_string(n) + ".txt", marked);
}
bool readChapter(const std::string& id, int n, std::string& marked) {
  return fs::readFile(cacheDir(id) + "/ch" + std::to_string(n) + ".txt", marked);
}

bool writeProgress(const std::string& id, const Progress& p) {
  JsonDocument doc;
  doc["chapter"] = p.chapter;
  doc["para"] = p.pos.para;
  doc["byte"] = p.pos.byte;
  doc["percent"] = p.percent;
  doc["opened"] = p.opened;
  std::string out;
  serializeJson(doc, out);
  return fs::writeFile(cacheDir(id) + "/progress.json", out);
}

bool readProgress(const std::string& id, Progress& p) {
  std::string data;
  if (!fs::readFile(cacheDir(id) + "/progress.json", data)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, data)) return false;
  p.chapter = doc["chapter"] | 0;
  p.pos.para = doc["para"] | 0;
  p.pos.byte = doc["byte"] | 0;
  p.percent = doc["percent"] | 0;
  p.opened = doc["opened"] | static_cast<int64_t>(0);
  return true;
}

std::string layoutKey(const ui::TextStyle& s) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "t%d-%dx%d-l%d-i%d-g%d", static_cast<int>(s.body), s.width, s.height,
                static_cast<int>(s.lineSpacing * 100 + 0.5f), s.indent, s.paragraphGap);
  return buf;
}

namespace {
void putU32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; ++i) s += static_cast<char>((v >> (8 * i)) & 0xFF);
}
uint32_t getU32(const std::string& s, size_t at) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(static_cast<unsigned char>(s[at + i])) << (8 * i);
  return v;
}
}  // namespace

std::string encodeIndex(const std::vector<PageRef>& pages) {
  std::string out = "PIX1";
  putU32(out, static_cast<uint32_t>(pages.size()));
  for (const PageRef& p : pages) {
    out += static_cast<char>(p.chapter & 0xFF);
    out += static_cast<char>(p.chapter >> 8);
    putU32(out, static_cast<uint32_t>(p.pos.para));
    putU32(out, static_cast<uint32_t>(p.pos.byte));
  }
  return out;
}

bool decodeIndex(const std::string& data, std::vector<PageRef>& pages) {
  pages.clear();
  if (data.size() < 8 || data.compare(0, 4, "PIX1") != 0) return false;
  const uint32_t n = getU32(data, 4);
  if (data.size() != 8 + static_cast<size_t>(n) * 10) return false;
  pages.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    const size_t at = 8 + static_cast<size_t>(i) * 10;
    PageRef r;
    r.chapter = static_cast<uint16_t>(static_cast<unsigned char>(data[at]) | (static_cast<unsigned char>(data[at + 1]) << 8));
    r.pos.para = static_cast<int>(getU32(data, at + 2));
    r.pos.byte = static_cast<int>(getU32(data, at + 6));
    pages.push_back(r);
  }
  return true;
}

bool writeIndex(const std::string& id, const std::string& key, const std::vector<PageRef>& pages) {
  return fs::writeFile(cacheDir(id) + "/pages-" + key + ".idx", encodeIndex(pages));
}
bool readIndex(const std::string& id, const std::string& key, std::vector<PageRef>& pages) {
  std::string data;
  if (!fs::readFile(cacheDir(id) + "/pages-" + key + ".idx", data)) return false;
  return decodeIndex(data, pages);
}

int CachedMetrics::width(const std::string& utf8, ui::Type type, bool bold, bool italic) {
  uint64_t h = 1469598103934665603ull;
  auto mix = [&h](unsigned char c) {
    h ^= c;
    h *= 1099511628211ull;
  };
  mix(static_cast<unsigned char>(type));
  mix(bold ? 1 : 0);
  mix(italic ? 1 : 0);
  for (unsigned char c : utf8) mix(c);
  if (h == 0) h = 1;
  Slot& s = table_[h % table_.size()];
  if (s.key == h) {
    ++hits_;
    return s.width;
  }
  ++misses_;
  s.key = h;
  const auto t = std::chrono::steady_clock::now();
  s.width = inner_.width(utf8, type, bold, italic);
  measureUs_ += static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t).count());
  return s.width;
}

std::vector<PageRef> buildIndex(const BookMeta& meta, const ui::TextStyle& style, ui::TextMetrics& metrics,
                                const std::function<bool(int, int)>& progress, const std::vector<std::string>* texts) {
  std::vector<PageRef> out;
  const int n = static_cast<int>(meta.chapters.size());
  ChapterLoader loader(meta);
  for (int ch = 0; ch < n; ++ch) {
    if (progress && !progress(ch, n)) return {};
    std::string loaded;
    const std::string* text = &loaded;
    auto tr = TimerClock::now();
    if (texts && ch < static_cast<int>(texts->size())) text = &(*texts)[ch];
    else if (!loader.load(ch, loaded)) loaded.clear();
    timing().readMs += since(tr);
    auto tp = TimerClock::now();
    std::vector<ui::Paragraph> paras = ui::parseMarked(*text);
    timing().parseMs += since(tp);
    if (paras.empty()) continue;  // an empty chapter has no pages
    bool stopped = false;
    auto tl = TimerClock::now();
    const std::vector<ui::TextPos> starts = ui::paginate(paras, style, metrics, [&] {
      cooperate();
      if (progress && !progress(ch, n)) stopped = true;
      return !stopped;
    });
    timing().layoutMs += since(tl);
    if (stopped) return {};
    for (const ui::TextPos& s : starts) out.push_back(PageRef{static_cast<uint16_t>(ch), s});
  }
  if (out.empty()) out.push_back(PageRef{});
  return out;
}

int pageIndexFor(const std::vector<PageRef>& pages, int chapter, ui::TextPos pos) {
  int idx = 0;
  for (size_t i = 0; i < pages.size(); ++i) {
    const PageRef& p = pages[i];
    if (p.chapter < chapter || (p.chapter == chapter && !(pos < p.pos))) idx = static_cast<int>(i);
    else break;
  }
  return idx;
}

int percentOf(int pageIndex, size_t pageCount) {
  if (pageCount <= 1) return pageCount == 1 && pageIndex >= 0 ? 0 : 0;
  return std::min(100, std::max(0, pageIndex * 100 / static_cast<int>(pageCount - 1)));
}

std::vector<LibraryEntry> scanLibrary() {
  std::vector<LibraryEntry> out;
  // The books folder and its folders (Files over Wi-Fi can make them), three levels deep; names starting with a dot are not
  // looked into (the reader's own .paperos cache, uploads in progress). One flat list: `file` is the path inside the folder.
  struct Pending {
    std::string rel;
    int depth;
  };
  std::vector<Pending> todo{{"", 0}};
  std::vector<std::pair<std::string, std::string>> books;  // relative path, name
  while (!todo.empty()) {
    const Pending d = todo.back();
    todo.pop_back();
    std::vector<fs::DirEntry> files;
    if (!fs::listDir(std::string(kBooksDir) + (d.rel.empty() ? "" : "/" + d.rel), files)) continue;
    for (const fs::DirEntry& f : files) {
      if (f.name.empty() || f.name[0] == '.') continue;
      const std::string rel = d.rel.empty() ? f.name : d.rel + "/" + f.name;
      if (f.isDir) {
        if (d.depth < 3) todo.push_back({rel, d.depth + 1});
      } else if (isBookFile(f.name)) {
        books.emplace_back(rel, f.name);
      }
    }
  }
  for (const auto& b : books) {
    uint64_t size = 0;
    int64_t mtime = 0;
    if (!fs::fileInfo(std::string(kBooksDir) + "/" + b.first, size, mtime)) continue;
    LibraryEntry e;
    e.file = b.first;
    e.id = bookId(b.first, size, mtime);
    e.epub = isEpubFile(b.second);
    titleFromFileName(b.second, e.title, e.author);
    BookMeta meta;
    if (readMeta(e.id, meta)) {
      if (!meta.title.empty()) e.title = meta.title;
      if (!meta.author.empty()) e.author = meta.author;
      e.cover = coverFile(meta);
    }
    Progress p;
    if (readProgress(e.id, p)) {
      e.percent = p.percent;
      e.opened = p.opened;
    }
    out.push_back(std::move(e));
  }
  sortLibrary(out);
  return out;
}

void sortLibrary(std::vector<LibraryEntry>& entries) {
  std::stable_sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
    if (a.opened != b.opened) return a.opened > b.opened;
    return lower(a.title) < lower(b.title);
  });
}

OpenResult openBook(const std::string& fileName, const ui::TextStyle& style, ui::TextMetrics& metrics,
                    const std::function<bool(int, int)>& step) {
  OpenResult r;
  timing() = Timing{};
  const std::string path = std::string(kBooksDir) + "/" + fileName;
  uint64_t size = 0;
  int64_t mtime = 0;
  if (!fs::fileInfo(path, size, mtime)) {
    r.error = "Cannot open " + fileName;
    return r;
  }
  const std::string id = bookId(fileName, size, mtime);
  BookMeta meta;
  const bool cached = readMeta(id, meta);
  if (cached) meta.file = fileName;  // moved into or out of a folder since: the cache is the book's, the place is new
  int stepBase = 0, stepTotal = 1;
  std::vector<std::string> fresh;  // chapter texts just converted: paginated from memory instead of read back
  auto report = [&](int done) { return !step || step(stepBase + done, stepTotal); };
  using Clock = std::chrono::steady_clock;
  auto msSince = [](Clock::time_point t) { return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count()); };
  const auto t0 = Clock::now();
  if (!cached) {
    meta = BookMeta{};
    meta.id = id;
    meta.file = fileName;
    meta.epub = isEpubFile(fileName);
    titleFromFileName(baseName(fileName), meta.title, meta.author);
    if (meta.epub) {
      auto ts = TimerClock::now();
      const bool structureOk = epubMeta(path, meta, r.error);
      timing().structureMs = since(ts);
      if (!structureOk) return r;
      stepTotal = 2 * static_cast<int>(meta.chapters.size());
      if (!convertEpub(path, meta, fresh, r.error, [&](int i, int) { return !step || step(i, stepTotal); })) return r;
      stepBase = stepTotal / 2;
    } else {
      std::string raw;
      if (!fs::readFile(path, raw)) {
        r.error = "Cannot read " + fileName;
        return r;
      }
      TxtImport imp = importTxt(txtToUtf8(raw));
      raw.clear();
      if (baseName(fileName).find(" - ") == std::string::npos) {  // no "Title - Author" in the name: use the file's own header
        if (!imp.title.empty()) meta.title = imp.title;
        if (!imp.author.empty()) meta.author = imp.author;
      }
      auto tw = TimerClock::now();
      for (size_t i = 0; i < imp.chapters.size(); ++i) {
        meta.chapters.push_back(Chapter{imp.chapters.size() == 1 ? meta.title : "Part " + std::to_string(i + 1), "", false, 0, 0});
        if (!writeChapter(id, static_cast<int>(i), imp.chapters[i])) {
          r.error = "Cannot write to the card";
          return r;
        }
      }
      timing().writeMs += since(tw);
      fresh = std::move(imp.chapters);
      stepTotal = 2 * static_cast<int>(meta.chapters.size());
      stepBase = stepTotal / 2;
    }
    auto tm = TimerClock::now();
    const bool metaOk = writeMeta(meta);
    timing().metaMs = since(tm);
    if (!metaOk) {
      r.error = "Cannot write to the card";
      return r;
    }
  } else {
    stepTotal = static_cast<int>(meta.chapters.size());
  }
  r.importMs = cached ? 0 : msSince(t0);
  const std::string key = layoutKey(style);
  std::vector<PageRef> pages;
  const bool indexCached = cached && readIndex(id, key, pages) && !pages.empty();
  if (!indexCached) {
    const auto t1 = Clock::now();
    CachedMetrics cm(metrics);
    pages = buildIndex(meta, style, cm, [&](int ch, int) { return report(ch); }, fresh.empty() ? nullptr : &fresh);
    if (pages.empty()) {
      r.error = "Cancelled";
      return r;
    }
    writeIndex(id, key, pages);
    r.indexMs = msSince(t1);
    timing().measureMs = cm.measureMs();
    timing().hits = cm.hits();
    timing().misses = cm.misses();
  }
  r.fromCache = indexCached;
  r.timing = timing();
  readProgress(id, r.progress);
  r.meta = std::move(meta);
  r.pages = std::move(pages);
  r.ok = true;
  return r;
}

}  // namespace pos::reader
