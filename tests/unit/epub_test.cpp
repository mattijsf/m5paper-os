#include <doctest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

#include "apps/reader/epub.h"
#include "apps/reader/xml.h"
#include "core/fs.h"
#include "core/zip.h"

using namespace pos;
using namespace pos::reader;

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Card {
  std::string dir;
  Card() {
    char tmpl[] = "/tmp/pos-ep-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~Card() {
    const std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
  // Puts bytes on the card as /sd/books/<name>.
  std::string put(const std::string& name, const std::string& data) {
    fs::writeFile("/sd/books/" + name, data);
    return "/sd/books/" + name;
  }
};

const char* kFixture3 = "tests/fixtures/books/fixture3.epub";
const char* kFixture2 = "tests/fixtures/books/fixture2.epub";

}  // namespace

TEST_CASE("xml: tags, attributes, entities, comments, CDATA, self-closing") {
  const std::string doc =
      "<?xml version=\"1.0\"?><!DOCTYPE html [<!ENTITY x \"y\">]><A Href='x&amp;y' b=\"1\" C=unquoted d><!-- <p>hidden</p> -->"
      "T&eacute;xt &#169; &#x41; &bogus; &amp<br/><![CDATA[<raw & text>]]></A>";
  xml::Tokenizer tk(doc);
  xml::Event e;
  REQUIRE(tk.next(e));
  CHECK(e.type == xml::Event::Start);
  CHECK(e.name == "a");
  CHECK(e.attr("href") == "x&y");
  CHECK(e.attr("b") == "1");
  CHECK(e.attr("c") == "unquoted");
  CHECK(e.attrs.size() == 4);
  REQUIRE(tk.next(e));
  CHECK(e.type == xml::Event::Text);
  CHECK(e.text == "T\xC3\xA9xt \xC2\xA9 A &bogus; &amp");
  REQUIRE(tk.next(e));
  CHECK(e.type == xml::Event::Start);
  CHECK(e.name == "br");
  CHECK(e.selfClosing);
  REQUIRE(tk.next(e));
  CHECK(e.type == xml::Event::End);
  CHECK(e.name == "br");
  REQUIRE(tk.next(e));
  CHECK(e.type == xml::Event::Text);
  CHECK(e.text == "<raw & text>");
  REQUIRE(tk.next(e));
  CHECK(e.type == xml::Event::End);
  CHECK(e.name == "a");
  CHECK_FALSE(tk.next(e));
}

TEST_CASE("xml: broken input never loops or reads past the end") {
  const char* bad[] = {"<", "<a", "<a b", "<a b=", "<a b=\"x", "</", "</a", "<!--", "<![CDATA[x", "<?xml", "<!DOCTYPE [", "&", "&#;", "&#x;", "<a/", "<<<>>>", "<a b c d e f g h>", "\xFF\xFE<a>", ""};
  for (const char* b : bad) {
    const std::string s = b;
    xml::Tokenizer tk(s);
    xml::Event e;
    int guard = 0;
    while (tk.next(e)) CHECK(++guard < 1000);
  }
  std::string noise;
  unsigned x = 99;
  for (int i = 0; i < 20000; ++i) {
    x = x * 1664525u + 1013904223u;
    noise += "<>/=\"'&;! abc"[(x >> 16) % 14];
  }
  xml::Tokenizer tk(noise);
  xml::Event e;
  int guard = 0;
  while (tk.next(e)) CHECK(++guard < 100000);
}

TEST_CASE("xml: entity decoding") {
  CHECK(xml::decodeEntities("a &amp; b") == "a & b");
  CHECK(xml::decodeEntities("&lt;&gt;&quot;&apos;") == "<>\"'");
  CHECK(xml::decodeEntities("&#8217;") == "\xE2\x80\x99");
  CHECK(xml::decodeEntities("&#x20AC;") == "\xE2\x82\xAC");
  CHECK(xml::decodeEntities("&mdash;&hellip;") == "\xE2\x80\x94\xE2\x80\xA6");
  CHECK(xml::decodeEntities("&#0;") == "\xEF\xBF\xBD");      // NUL is not allowed: replacement character
  CHECK(xml::decodeEntities("&#xD800;") == "\xEF\xBF\xBD");  // surrogate
  CHECK(xml::decodeEntities("&#99999999;") == "&#99999999;");
  CHECK(xml::decodeEntities("no entity") == "no entity");
  CHECK(xml::localName("dc:title") == "title");
  CHECK(xml::localName("title") == "title");
}

TEST_CASE("epub: href resolution") {
  CHECK(resolveHref("OEBPS/", "text/ch1.xhtml") == "OEBPS/text/ch1.xhtml");
  CHECK(resolveHref("OEBPS/text/", "../images/c.png") == "OEBPS/images/c.png");
  CHECK(resolveHref("OEBPS/", "ch%201.xhtml#frag") == "OEBPS/ch 1.xhtml");
  CHECK(resolveHref("OEBPS/", "./a.xhtml?x=1") == "OEBPS/a.xhtml");
  CHECK(resolveHref("OEBPS/", "/abs/a.xhtml") == "abs/a.xhtml");
  CHECK(resolveHref("", "../../../x") == "x");  // cannot climb out of the zip
  CHECK(resolveHref("OEBPS/", "#frag") == "");
  CHECK(resolveHref("OEBPS/", "") == "");
}

TEST_CASE("zip: directory, stored and deflated entries, checksums") {
  Card card;
  const std::string data = slurp(kFixture3);
  REQUIRE(!data.empty());
  zip::Archive z;
  REQUIRE(z.open(card.put("f.epub", data)));
  CHECK(z.entries().size() == 9);
  const zip::Entry* mt = z.find("mimetype");
  REQUIRE(mt);
  CHECK(mt->method == 0);
  std::string s;
  REQUIRE(z.read(*mt, s));
  CHECK(s == "application/epub+zip");
  const zip::Entry* ch2 = z.find("OEBPS/text/ch2.xhtml");
  REQUIRE(ch2);
  CHECK(ch2->method == 8);
  REQUIRE(z.read(*ch2, s));
  CHECK(s.size() == ch2->size);
  CHECK(s.find("<h2 id=\"top\">Second chapter</h2>") != std::string::npos);
  CHECK(zip::crc32(0, reinterpret_cast<const uint8_t*>(s.data()), s.size()) == ch2->crc);
  CHECK(z.find("/mimetype") == mt);  // leading slash tolerated
  CHECK(z.find("nope") == nullptr);
  CHECK_FALSE(z.read("nope", s));
  // A size limit refuses big entries.
  CHECK_FALSE(z.read(*ch2, s, 100));
  // The sink can stop early.
  int calls = 0;
  CHECK_FALSE(z.extract(*ch2, [&](const uint8_t*, size_t) { ++calls; return false; }));
  CHECK(calls == 1);
}

TEST_CASE("zip: malformed files fail cleanly") {
  Card card;
  const std::string good = slurp(kFixture3);
  REQUIRE(!good.empty());
  zip::Archive z;
  CHECK_FALSE(z.open("/sd/books/missing.epub"));
  CHECK_FALSE(z.open(card.put("empty.epub", "")));
  CHECK_FALSE(z.open(card.put("tiny.epub", "PK")));
  CHECK_FALSE(z.open(card.put("text.epub", std::string(5000, 'x'))));
  CHECK_FALSE(z.open(card.put("cut.epub", good.substr(0, good.size() / 2))));       // no directory at the end
  CHECK_FALSE(z.open(card.put("cut2.epub", good.substr(0, good.size() - 5))));       // directory end cut
  // Corrupt compressed data of one entry: the directory opens, that entry does not.
  {
    std::string bad = good;
    zip::Archive ok;
    REQUIRE(ok.open(card.put("ok.epub", good)));
    const zip::Entry* e = ok.find("OEBPS/text/ch2.xhtml");
    REQUIRE(e);
    for (size_t i = 0; i < 30; ++i) bad[e->localOffset + 40 + 20 + i] ^= 0x5A;
    zip::Archive b;
    REQUIRE(b.open(card.put("flip.epub", bad)));
    std::string s;
    CHECK_FALSE(b.read("OEBPS/text/ch2.xhtml", s));
    CHECK_FALSE(b.error().empty());
    CHECK(b.read("mimetype", s));  // the others still read
  }
  // An entry pointing past the end of the file.
  {
    zip::Archive ok;
    REQUIRE(ok.open(card.put("ok2.epub", good)));
    zip::Entry e = *ok.find("OEBPS/text/ch2.xhtml");
    e.compressedSize = 0x7FFFFFFF;
    std::string s;
    CHECK_FALSE(ok.read(e, s));
    e = *ok.find("OEBPS/text/ch2.xhtml");
    e.localOffset = 5;  // not a local header
    CHECK_FALSE(ok.read(e, s));
    e = *ok.find("OEBPS/text/ch2.xhtml");
    e.size += 1;  // wrong declared size
    CHECK_FALSE(ok.read(e, s));
    e = *ok.find("OEBPS/text/ch2.xhtml");
    e.crc ^= 1;
    CHECK_FALSE(ok.read(e, s));
    e = *ok.find("OEBPS/text/ch2.xhtml");
    e.method = 9;  // not supported
    CHECK_FALSE(ok.read(e, s));
  }
  // Random damage anywhere: opening and reading every entry must never crash or hang.
  unsigned x = 7;
  for (int round = 0; round < 60; ++round) {
    std::string bad = good;
    for (int k = 0; k < 4; ++k) {
      x = x * 1664525u + 1013904223u;
      bad[(x >> 8) % bad.size()] = static_cast<char>(x >> 20);
    }
    zip::Archive r;
    if (!r.open(card.put("rnd.epub", bad))) continue;
    for (const zip::Entry& en : r.entries()) {
      std::string s;
      r.read(en, s);
    }
  }
}

TEST_CASE("epub 3: metadata, cover, spine and the nav table of contents") {
  Card card;
  zip::Archive z;
  REQUIRE(z.open(card.put("f.epub", slurp(kFixture3))));
  EpubInfo info;
  std::string err;
  REQUIRE(parseEpub(z, info, err));
  CHECK(info.title == "The Fixture & Co");
  CHECK(info.author == "Ann Example");  // the translator is not an author
  CHECK(info.opfPath == "OEBPS/content.opf");
  CHECK(info.cover == "OEBPS/images/cover.png");
  CHECK(info.coverType == "image/png");
  REQUIRE(info.chapters.size() == 3);  // the "ghost" itemref has no manifest item
  CHECK(info.chapters[0].href == "OEBPS/text/Chapter One.xhtml");
  CHECK(info.chapters[0].title == "Chapter & One");
  CHECK(info.chapters[1].title == "The Second Chapter");  // first entry for the document, white space squeezed
  CHECK(info.chapters[2].title == "The End");               // not "Landmark, not the TOC"
}

TEST_CASE("epub 2: ncx, cover meta, stored chapter") {
  Card card;
  zip::Archive z;
  REQUIRE(z.open(card.put("o.epub", slurp(kFixture2))));
  EpubInfo info;
  std::string err;
  REQUIRE(parseEpub(z, info, err));
  CHECK(info.title == "Old Style");
  CHECK(info.author == "Old Writer");
  CHECK(info.cover == "cov.png");
  REQUIRE(info.chapters.size() == 1);
  CHECK(info.chapters[0].href == "a.html");
  CHECK(info.chapters[0].title == "Only chapter");
}

TEST_CASE("epub: files that are not usable EPUBs say why") {
  Card card;
  zip::Archive z;
  EpubInfo info;
  std::string err;
  // A zip without container.xml
  const std::string good = slurp(kFixture3);
  std::string noContainer = good;
  const size_t at = noContainer.find("META-INF/container.xml");
  REQUIRE(at != std::string::npos);
  noContainer.replace(at, 22, "META-INF/containex.xml");  // both the local and the directory name
  noContainer.replace(noContainer.find("META-INF/container.xml"), 22, "META-INF/containex.xml");
  REQUIRE(z.open(card.put("nc.epub", noContainer)));
  CHECK_FALSE(parseEpub(z, info, err));
  CHECK(err.find("container") != std::string::npos);
  // Not a zip at all is caught when opening (zip tests); here: an OPF without spine
  std::string opfless = good;
  const size_t o = opfless.find("OEBPS/content.opf");
  REQUIRE(o != std::string::npos);
  opfless.replace(o, 17, "OEBPS/content.opx");
  opfless.replace(opfless.find("OEBPS/content.opf"), 17, "OEBPS/content.opx");
  REQUIRE(z.open(card.put("no-opf.epub", opfless)));
  CHECK_FALSE(parseEpub(z, info, err));
  CHECK_FALSE(err.empty());
}

TEST_CASE("epub: the real books parse (when they are in sim-data/sd/books)") {
  struct Want {
    const char* file;
    const char* title;
    const char* author;
  };
  const Want books[] = {{"Pride and Prejudice - Jane Austen.epub", "Pride and Prejudice", "Jane Austen"},
                        {"The Adventures of Sherlock Holmes - Arthur Conan Doyle.epub", "Adventures of Sherlock Holmes", "Doyle"},
                        {"Frankenstein - Mary Shelley.epub", "Frankenstein", "Shelley"},
                        {"Alice's Adventures in Wonderland - Lewis Carroll.epub", "Alice", "Carroll"},
                        {"Max Havelaar - Multatuli.epub", "Max Havelaar", "Multatuli"}};
  fs::setRoots("sim-data/sd", "sim-data/data");
  int seen = 0;
  for (const Want& w : books) {
    zip::Archive z;
    if (!z.open(std::string("/sd/books/") + w.file)) continue;  // not downloaded: skip
    ++seen;
    EpubInfo info;
    std::string err;
    INFO(w.file << ": " << err);
    REQUIRE(parseEpub(z, info, err));
    CHECK(info.title.find(w.title) != std::string::npos);
    CHECK(info.author.find(w.author) != std::string::npos);
    CHECK(info.chapters.size() >= 3);
    for (const EpubChapter& c : info.chapters) {
      std::string s;
      CHECK(z.read(c.href, s));
    }
  }
  MESSAGE("real EPUBs found: " << seen);
}
