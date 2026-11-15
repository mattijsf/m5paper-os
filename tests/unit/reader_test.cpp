#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "apps/reader/book.h"
#include "core/fs.h"

using namespace pos;
using namespace pos::reader;

namespace {

class FakeMetrics : public ui::TextMetrics {
 public:
  int width(const std::string& s, ui::Type, bool, bool) override {
    int n = 0;
    for (unsigned char c : s)
      if ((c & 0xC0) != 0x80) ++n;
    return n * 10;
  }
  int lineHeight(ui::Type) override { return 20; }
};

struct Card {
  std::string dir;
  Card() {
    char tmpl[] = "/tmp/pos-rd-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~Card() {
    const std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

ui::TextStyle smallPage() {
  ui::TextStyle s;
  s.width = 300;
  s.height = 200;
  return s;
}

}  // namespace

TEST_CASE("reader: file names, ids, titles") {
  CHECK(isBookFile("a.epub"));
  CHECK(isBookFile("B.TXT"));
  CHECK_FALSE(isBookFile("a.pdf"));
  CHECK_FALSE(isBookFile(".hidden.epub"));
  CHECK(isEpubFile("x.EPUB"));
  CHECK_FALSE(isEpubFile("x.txt"));
  CHECK(bookId("a.txt", 10, 5).size() == 12);
  CHECK(bookId("a.txt", 10, 5) == bookId("a.txt", 10, 5));
  CHECK(bookId("a.txt", 10, 5) != bookId("a.txt", 11, 5));
  CHECK(bookId("a.txt", 10, 5) != bookId("a.txt", 10, 6));
  CHECK(bookId("a.txt", 10, 5) != bookId("b.txt", 10, 5));
  std::string t, a;
  titleFromFileName("Pride and Prejudice - Jane Austen.epub", t, a);
  CHECK(t == "Pride and Prejudice");
  CHECK(a == "Jane Austen");
  titleFromFileName("Alice's Adventures - In - Wonderland - Lewis Carroll.epub", t, a);
  CHECK(t == "Alice's Adventures - In - Wonderland");
  CHECK(a == "Lewis Carroll");
  titleFromFileName("no author here.txt", t, a);
  CHECK(t == "no author here");
  CHECK(a == "");
}

TEST_CASE("reader: text encoding") {
  CHECK(txtToUtf8("a\r\nb\rc") == "a\nb\nc");
  CHECK(txtToUtf8("\xEF\xBB\xBFhi") == "hi");
  CHECK(txtToUtf8("caf\xC3\xA9") == "caf\xC3\xA9");      // valid UTF-8 stays
  CHECK(txtToUtf8("caf\xE9") == "caf\xC3\xA9");          // Latin-1
  CHECK(txtToUtf8("\x93hi\x94") == "\xE2\x80\x9Chi\xE2\x80\x9D");  // Windows-1252 quotes
  CHECK(txtToUtf8("tr\xC3") == "tr\xC3\x83");            // truncated sequence: not UTF-8, read as Latin-1
  CHECK(txtToUtf8("") == "");
}

TEST_CASE("reader: TXT import joins hard-wrapped lines, finds headings and italics, strips Gutenberg text") {
  const std::string src =
      "Title: A Tale\nAuthor: Some One\n\nlicense blah\n*** START OF THE PROJECT GUTENBERG EBOOK A TALE ***\n\n"
      "CHAPTER I.\n\nThe first line of text\nwraps here and has _italic words_ in it.\n\n\n\n"
      "Second  paragraph\twith   gaps.\n\n*** END OF THE PROJECT GUTENBERG EBOOK A TALE ***\nlicense text\n";
  TxtImport r = importTxt(src);
  CHECK(r.title == "A Tale");
  CHECK(r.author == "Some One");
  REQUIRE(r.chapters.size() == 1);
  auto paras = ui::parseMarked(r.chapters[0]);
  REQUIRE(paras.size() == 3);
  CHECK(paras[0].heading == 2);
  CHECK(ui::plainText(paras[0]) == "CHAPTER I.");
  CHECK(ui::plainText(paras[1]) == "The first line of text wraps here and has italic words in it.");
  bool sawItalic = false;
  for (const auto& run : paras[1].runs)
    if (run.italic) {
      sawItalic = true;
      CHECK(run.text == "italic words");
    }
  CHECK(sawItalic);
  CHECK(ui::plainText(paras[2]) == "Second paragraph with gaps.");
  CHECK(r.chapters[0].find("license") == std::string::npos);
}

TEST_CASE("reader: TXT import keeps odd underscores, drops control bytes, handles empty input") {
  TxtImport r = importTxt("snake_case and _ alone\x01\x05x\n");
  auto p = ui::parseMarked(r.chapters[0]);
  REQUIRE(p.size() == 1);
  CHECK(p[0].heading == 0);
  CHECK(ui::plainText(p[0]).find("snake_case") == 0);
  TxtImport e = importTxt("");
  CHECK(e.chapters.size() == 1);
  CHECK(ui::parseMarked(e.chapters[0]).empty());
}

TEST_CASE("reader: a long text is split into parts at paragraph boundaries") {
  std::string src;
  for (int i = 0; i < 100; ++i) src += "Paragraph number " + std::to_string(i) + " with some words in it.\n\n";
  TxtImport r = importTxt(src, 500);
  CHECK(r.chapters.size() > 3);
  size_t total = 0;
  for (const auto& c : r.chapters) {
    CHECK(c.size() <= 500 + 80);
    total += ui::parseMarked(c).size();
  }
  CHECK(total == 100);
}

TEST_CASE("reader: page index file round trip and bad files") {
  std::vector<PageRef> pages = {{0, {0, 0}}, {0, {3, 17}}, {2, {0, 0}}, {300, {70000, 123456}}};
  std::string enc = encodeIndex(pages);
  std::vector<PageRef> back;
  REQUIRE(decodeIndex(enc, back));
  CHECK(back == pages);
  CHECK_FALSE(decodeIndex("", back));
  CHECK_FALSE(decodeIndex("PIX1", back));
  CHECK_FALSE(decodeIndex(enc.substr(0, enc.size() - 1), back));
  std::string bad = enc;
  bad[0] = 'X';
  CHECK_FALSE(decodeIndex(bad, back));
}

TEST_CASE("reader: position to page") {
  std::vector<PageRef> pages = {{0, {0, 0}}, {0, {3, 17}}, {1, {0, 0}}, {1, {5, 0}}};
  CHECK(pageIndexFor(pages, 0, {0, 0}) == 0);
  CHECK(pageIndexFor(pages, 0, {3, 16}) == 0);
  CHECK(pageIndexFor(pages, 0, {3, 17}) == 1);
  CHECK(pageIndexFor(pages, 0, {99, 0}) == 1);  // later in chapter 0: its last page
  CHECK(pageIndexFor(pages, 1, {0, 0}) == 2);
  CHECK(pageIndexFor(pages, 1, {7, 3}) == 3);
  CHECK(pageIndexFor(pages, 5, {0, 0}) == 3);   // a chapter past the end: the last page
  CHECK(pageIndexFor({}, 0, {0, 0}) == 0);
  CHECK(percentOf(0, 10) == 0);
  CHECK(percentOf(9, 10) == 100);
  CHECK(percentOf(0, 1) == 0);
  CHECK(percentOf(4, 9) == 50);
}

TEST_CASE("reader: open a TXT book, cache it, reopen from the cache, resume") {
  Card card;
  std::string text = "*** START OF THE PROJECT GUTENBERG EBOOK T ***\n";
  for (int i = 0; i < 60; ++i) text += "Paragraph " + std::to_string(i) + " has a few words to fill lines and make pages go on.\n\n";
  REQUIRE(fs::writeFile("/sd/books/Some Book - An Author.txt", text));
  FakeMetrics m;
  OpenResult a = openBook("Some Book - An Author.txt", smallPage(), m);
  REQUIRE(a.ok);
  CHECK(a.meta.title == "Some Book");
  CHECK(a.meta.author == "An Author");
  CHECK(a.meta.chapters.size() == 1);
  CHECK(a.pages.size() > 5);
  CHECK_FALSE(a.fromCache);
  CHECK(a.progress.opened == 0);
  CHECK(fs::exists(cacheDir(a.meta.id) + "/meta.json"));
  CHECK(fs::exists(cacheDir(a.meta.id) + "/ch0.txt"));
  CHECK(fs::exists(cacheDir(a.meta.id) + "/pages-" + layoutKey(smallPage()) + ".idx"));

  Progress p;
  p.chapter = 0;
  p.pos = a.pages[3].pos;
  p.percent = 33;
  p.opened = 1700000000;
  REQUIRE(writeProgress(a.meta.id, p));

  OpenResult b = openBook("Some Book - An Author.txt", smallPage(), m);
  REQUIRE(b.ok);
  CHECK(b.fromCache);
  CHECK(b.pages == a.pages);
  CHECK(b.progress.opened == 1700000000);
  CHECK(pageIndexFor(b.pages, b.progress.chapter, b.progress.pos) == 3);

  // Another font size: new index, same book, the reading position still finds its page.
  ui::TextStyle big = smallPage();
  big.body = ui::Type::ReadXL;
  big.width = 200;
  OpenResult c = openBook("Some Book - An Author.txt", big, m);
  REQUIRE(c.ok);
  CHECK(c.meta.id == a.meta.id);  // same book, its meta is reused; only the page index is new
  CHECK_FALSE(c.fromCache);
  CHECK(c.pages.size() > a.pages.size());
  const int idx = pageIndexFor(c.pages, b.progress.chapter, b.progress.pos);
  CHECK(!(b.progress.pos < c.pages[idx].pos));

  // The same file name but another size is another book.
  fs::writeFile("/sd/books/Some Book - An Author.txt", text + "more\n");
  OpenResult d = openBook("Some Book - An Author.txt", smallPage(), m);
  REQUIRE(d.ok);
  CHECK(d.meta.id != a.meta.id);
}

TEST_CASE("reader: open errors") {
  Card card;
  FakeMetrics m;
  CHECK_FALSE(openBook("missing.txt", smallPage(), m).ok);
  fs::writeFile("/sd/books/x.epub", "not a zip");
  OpenResult r = openBook("x.epub", smallPage(), m);
  CHECK_FALSE(r.ok);
  CHECK_FALSE(r.error.empty());
  // An empty text file still opens: one empty page, no crash.
  fs::writeFile("/sd/books/empty - nobody.txt", "");
  OpenResult e = openBook("empty - nobody.txt", smallPage(), m);
  REQUIRE(e.ok);
  CHECK(e.pages.size() == 1);
}

TEST_CASE("reader: progress file round trip") {
  Card card;
  Progress p;
  p.chapter = 4;
  p.pos = {12, 340};
  p.percent = 57;
  p.opened = 1760000000;
  REQUIRE(writeProgress("abc123abc123", p));
  Progress q;
  REQUIRE(readProgress("abc123abc123", q));
  CHECK(q.chapter == 4);
  CHECK(q.pos == p.pos);
  CHECK(q.percent == 57);
  CHECK(q.opened == 1760000000);
  Progress none;
  CHECK_FALSE(readProgress("000000000000", none));
}

TEST_CASE("reader: library scan reads names, cached meta and progress, and sorts by last opened") {
  Card card;
  CHECK(scanLibrary().empty());  // no folder
  fs::writeFile("/sd/books/Zebra Tales - Zed Author.txt", "z\n");
  fs::writeFile("/sd/books/alpha Tales - Al Author.txt", "a\n");
  fs::writeFile("/sd/books/Middle - Mid Author.txt", "m\n");
  fs::writeFile("/sd/books/notes.pdf", "x");
  fs::writeFile("/sd/books/.hidden.txt", "x");
  fs::writeFile("/sd/books/.paperos/xx/meta.json", "{}");
  auto lib = scanLibrary();
  REQUIRE(lib.size() == 3);
  CHECK(lib[0].title == "alpha Tales");  // never opened: by title, case-insensitive
  CHECK(lib[1].title == "Middle");
  CHECK(lib[2].title == "Zebra Tales");
  CHECK(lib[2].author == "Zed Author");
  CHECK(lib[0].opened == 0);

  // Opening Zebra Tales and reading to 40% makes it the most recent; its cached title wins over the file name.
  FakeMetrics m;
  OpenResult o = openBook("Zebra Tales - Zed Author.txt", smallPage(), m);
  REQUIRE(o.ok);
  Progress p;
  p.percent = 40;
  p.opened = 1700000000;
  writeProgress(o.meta.id, p);
  BookMeta changed = o.meta;
  changed.title = "Zebra Tales (the real title)";
  writeMeta(changed);
  lib = scanLibrary();
  REQUIRE(lib.size() == 3);
  CHECK(lib[0].file == "Zebra Tales - Zed Author.txt");
  CHECK(lib[0].title == "Zebra Tales (the real title)");
  CHECK(lib[0].percent == 40);
  CHECK(lib[0].opened == 1700000000);
  CHECK(lib[1].title == "alpha Tales");
}

TEST_CASE("reader: EPUB structure becomes book meta and survives meta.json") {
  Card card;
  std::string epub;
  {
    FILE* f = std::fopen("tests/fixtures/books/fixture3.epub", "rb");
    REQUIRE(f);
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) epub.append(buf, n);
    std::fclose(f);
  }
  REQUIRE(fs::writeFile("/sd/books/Fixture - Someone.epub", epub));
  BookMeta m;
  m.id = "aabbccddeeff";
  m.file = "Fixture - Someone.epub";
  titleFromFileName(m.file, m.title, m.author);
  std::string err;
  REQUIRE(epubMeta("/sd/books/Fixture - Someone.epub", m, err));
  CHECK(m.epub);
  CHECK(m.title == "The Fixture & Co");  // the book's own metadata beats the file name
  CHECK(m.author == "Ann Example");
  CHECK(m.cover == "OEBPS/images/cover.png");
  REQUIRE(m.chapters.size() == 3);
  CHECK(m.chapters[1].title == "The Second Chapter");
  CHECK(m.chapters[1].href == "OEBPS/text/ch2.xhtml");
  REQUIRE(writeMeta(m));
  BookMeta back;
  REQUIRE(readMeta(m.id, back));
  CHECK(back.title == m.title);
  CHECK(back.cover == m.cover);
  CHECK(back.chapters.size() == 3);
  CHECK(back.chapters[0].href == "OEBPS/text/Chapter One.xhtml");
  CHECK(back.epub);

  fs::writeFile("/sd/books/junk.epub", "this is not a zip file at all, just some text");
  BookMeta j;
  CHECK_FALSE(epubMeta("/sd/books/junk.epub", j, err));
  CHECK(err.find("Not a valid EPUB") == 0);
}

// ---- XHTML

#include "apps/reader/xhtml.h"

namespace {
std::string plainOf(const std::string& marked) {
  std::string out;
  for (char c : marked)
    if (static_cast<unsigned char>(c) >= 0x20 || c == '\n') out += c;
  return out;
}
}  // namespace

TEST_CASE("xhtml: paragraphs, headings, styles, entities, white space") {
  XhtmlResult r = xhtmlToMarked(
      "<html><head><title>T</title><style>p{}</style><script>x<p>y</p></script></head><body>"
      "<h1>Chapter  <i>One</i></h1>\n"
      "<p>It was   a\n <b>bold</b> start &amp; an <em>italic</em> one.<!-- <p>no</p> --></p>"
      "<div>div text<br/>after br</div><p>   </p><p></p>"
      "<ul><li>first</li><li>second</li></ul><ol><li>one</li><li>two</li></ol><hr/>"
      "<p>pic <img src='a.png' alt='A cat'/> and <img src='b.png'/></p><svg><image href='c.png'/></svg>"
      "<table><tr><td>a</td><td>b</td></tr></table></body></html>");
  auto paras = ui::parseMarked(r.marked);
  std::vector<std::string> text;
  for (const auto& p : paras) text.push_back(ui::plainText(p));
  REQUIRE(text.size() >= 12);
  CHECK(text[0] == "Chapter One");
  CHECK(paras[0].heading == 1);
  CHECK(r.firstHeading == "Chapter One");
  CHECK(text[1] == "It was a bold start & an italic one.");
  bool bold = false, italic = false;
  for (const auto& run : paras[1].runs) {
    if (run.bold) { bold = true; CHECK(run.text == "bold"); }
    if (run.italic) { italic = true; CHECK(run.text == "italic"); }
  }
  CHECK(bold);
  CHECK(italic);
  CHECK(text[2] == "div text");
  CHECK(text[3] == "after br");
  CHECK_FALSE(paras[2].softBreak);
  CHECK(paras[3].softBreak);  // after a <br>: same block, no indent, no gap
  CHECK(text[4] == "\xE2\x80\xA2 first");
  CHECK(text[5] == "\xE2\x80\xA2 second");
  CHECK(text[6] == "1. one");
  CHECK(text[7] == "2. two");
  CHECK(text[8] == "* * *");
  CHECK(text[9] == "pic [image: A cat] and [image]");
  CHECK(text[10] == "[image]");
  CHECK(text[11] == "a | b");
  CHECK(r.marked.find("not text") == std::string::npos);
  CHECK(r.marked.find("<p>") == std::string::npos);
}

TEST_CASE("xhtml: style marks balance inside each paragraph, broken markup is survived") {
  // A bold run that spans paragraphs is closed and reopened for each of them.
  XhtmlResult r = xhtmlToMarked("<p>a <b>one</p><p>two</p><p>three</b> four</p>");
  auto paras = ui::parseMarked(r.marked);
  REQUIRE(paras.size() == 3);
  CHECK(ui::plainText(paras[0]) == "a one");
  CHECK(paras[1].runs.size() == 1);
  CHECK(paras[1].runs[0].bold);
  CHECK(paras[2].runs[0].bold);
  CHECK(paras[2].runs.back().text == " four");
  CHECK_FALSE(paras[2].runs.back().bold);
  const char* bad[] = {"", "<", "<p", "<p>text", "text only", "</p></div></b>", "<b><i>x", "<p><b>a</p>b</b>", "<h1>", "<ul><li>x", "<<>>"};
  for (const char* b : bad) {
    XhtmlResult x = xhtmlToMarked(b);
    ui::parseMarked(x.marked);
  }
  CHECK(plainOf(xhtmlToMarked("<p>a\x01\x02\x05z</p>").marked) == "a" "z\n");  // control bytes cannot forge marks
  // Headings: h1 -> 1, h2 -> 2, h3..h6 -> 3
  auto h = ui::parseMarked(xhtmlToMarked("<h1>a</h1><h2>b</h2><h4>c</h4>").marked);
  REQUIRE(h.size() == 3);
  CHECK(h[0].heading == 1);
  CHECK(h[1].heading == 2);
  CHECK(h[2].heading == 3);
}

TEST_CASE("reader: open an EPUB, cache it, reopen, cover, chapters from the contents and from headings") {
  Card card;
  std::string epub;
  {
    FILE* f = std::fopen("tests/fixtures/books/fixture3.epub", "rb");
    REQUIRE(f);
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) epub.append(buf, n);
    std::fclose(f);
  }
  REQUIRE(fs::writeFile("/sd/books/Fixture - Someone.epub", epub));
  FakeMetrics m;
  int lastDone = -1, lastTotal = 0;
  OpenResult a = openBook("Fixture - Someone.epub", smallPage(), m, [&](int done, int total) {
    CHECK(done >= lastDone);  // never goes backwards
    lastDone = done;
    lastTotal = total;
    return true;
  });
  REQUIRE(a.ok);
  CHECK_FALSE(a.fromCache);
  CHECK(a.meta.title == "The Fixture & Co");
  CHECK(a.meta.author == "Ann Example");
  CHECK(a.meta.epub);
  CHECK(a.meta.cover == "OEBPS/images/cover.png");
  CHECK(coverFile(a.meta).empty());  // not extracted yet: the reader does that in the background
  REQUIRE(extractCover(a.meta));
  CHECK(coverFile(a.meta) == cacheDir(a.meta.id) + "/cover.png");
  CHECK(fs::exists(cacheDir(a.meta.id) + "/cover.png"));
  REQUIRE(a.meta.chapters.size() == 3);
  CHECK(a.meta.chapters[0].title == "Chapter & One");
  CHECK(lastTotal == 6);  // 3 chapters imported, 3 paginated
  CHECK(lastDone >= 3);
  CHECK(a.pages.size() > 10);
  // Chapter text: headings, styles, images, entities. An EPUB writes no chapter files: the text is converted on demand.
  CHECK_FALSE(fs::exists(cacheDir(a.meta.id) + "/ch0.txt"));
  std::string ch;
  ChapterLoader loader(a.meta);
  REQUIRE(loader.load(0, ch));
  auto paras = ui::parseMarked(ch);
  REQUIRE(paras.size() > 5);
  CHECK(paras[0].heading == 1);
  CHECK(ui::plainText(paras[0]) == "Chapter One");
  CHECK(ui::plainText(paras[1]).find("caf\xC3\xA9 & cr\xC3\xA8me \xE2\x80\x94") != std::string::npos);
  CHECK(ch.find("[image: a picture]") != std::string::npos);
  CHECK(ch.find("not text") == std::string::npos);

  OpenResult b = openBook("Fixture - Someone.epub", smallPage(), m);
  REQUIRE(b.ok);
  CHECK(b.fromCache);
  CHECK(b.pages == a.pages);
  CHECK(b.meta.chapters.size() == 3);
  CHECK(b.meta.cover == "OEBPS/images/cover.png");
  // Another font size: a new index from the EPUB itself (no cache of the text).
  ui::TextStyle big = smallPage();
  big.body = ui::Type::ReadXL;
  big.width = 200;
  OpenResult bigger = openBook("Fixture - Someone.epub", big, m);
  REQUIRE(bigger.ok);
  CHECK(bigger.pages.size() > a.pages.size());
  // Cancelling leaves no half-built book behind that opens as if complete.
  fs::removeAll(std::string(kCacheRoot));
  OpenResult c = openBook("Fixture - Someone.epub", smallPage(), m, [](int, int) { return false; });
  CHECK_FALSE(c.ok);
  CHECK(c.error == "Cancelled");
  CHECK_FALSE(fs::exists(cacheDir(a.meta.id) + "/meta.json"));
  OpenResult d = openBook("Fixture - Someone.epub", smallPage(), m);
  CHECK(d.ok);
}

TEST_CASE("reader: cached widths give the same pages") {
  std::vector<ui::Paragraph> paras;
  for (int i = 0; i < 40; ++i) {
    ui::Paragraph p;
    p.runs.push_back({"the quick brown fox jumps over the lazy dog number " + std::to_string(i) + " and then some more words", i % 3 == 0, i % 5 == 0});
    paras.push_back(p);
  }
  FakeMetrics plain;
  CachedMetrics cached(plain);
  auto a = ui::paginate(paras, smallPage(), plain);
  auto b = ui::paginate(paras, smallPage(), cached);
  CHECK(a.size() == b.size());
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) CHECK(a[i] == b[i]);
}

TEST_CASE("reader: every real book opens and paginates (when they are in sim-data/sd/books)") {
  fs::setRoots("sim-data/sd", "sim-data/data");
  std::vector<fs::DirEntry> files;
  if (!fs::listDir("/sd/books", files)) return;
  // The cache of the real card stays untouched: the test copies each book to a temporary card.
  std::vector<std::pair<std::string, std::string>> books;
  for (const auto& f : files) {
    if (f.isDir || !isBookFile(f.name)) continue;
    std::string data;
    REQUIRE(fs::readFile("/sd/books/" + f.name, data));
    books.emplace_back(f.name, std::move(data));
  }
  int opened = 0;
  for (const auto& book : books) {
    const std::string& name = book.first;
    const std::string& data = book.second;
    Card card;
    REQUIRE(fs::writeFile("/sd/books/" + name, data));
    FakeMetrics m;
    OpenResult r = openBook(name, smallPage(), m);
    INFO(name << ": " << r.error);
    REQUIRE(r.ok);
    CHECK(r.pages.size() > 20);
    CHECK_FALSE(r.meta.title.empty());
    CHECK_FALSE(r.meta.author.empty());
    for (size_t i = 1; i < r.pages.size(); ++i) CHECK(!(r.pages[i] == r.pages[i - 1]));
    uint64_t hash = 1469598103934665603ull;  // fingerprint of the page breaks: layout changes must not move them by accident
    for (const PageRef& pg : r.pages)
      for (int v : {static_cast<int>(pg.chapter), pg.pos.para, pg.pos.byte}) hash = (hash ^ static_cast<uint64_t>(v)) * 1099511628211ull;
    MESSAGE(name << ": page break fingerprint " << std::hex << hash << std::dec);
    ++opened;
    MESSAGE(name << ": '" << r.meta.title << "' by '" << r.meta.author << "', " << r.meta.chapters.size() << " chapters, " << r.pages.size() << " pages");
  }
  MESSAGE("real books opened: " << opened);
}
