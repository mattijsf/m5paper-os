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
