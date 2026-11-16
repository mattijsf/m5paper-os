#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

#include "core/clipboard.h"
#include "core/fs.h"
#include "core/log.h"

using namespace pos;

namespace {
struct TmpRoots {
  std::string dir;
  TmpRoots() {
    char tmpl[] = "/tmp/pos-clip-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~TmpRoots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};
}  // namespace

TEST_CASE("clipboard: set, get, source, clear; empty at first") {
  TmpRoots t;
  CHECK(clipboard::empty());
  CHECK(clipboard::get() == "");
  CHECK(clipboard::source() == "");
  CHECK(clipboard::set("Woordle 4/6", "woordle"));
  CHECK_FALSE(clipboard::empty());
  CHECK(clipboard::get() == "Woordle 4/6");
  CHECK(clipboard::source() == "woordle");
  CHECK(clipboard::set("second", "field"));  // replaces
  CHECK(clipboard::get() == "second");
  CHECK(clipboard::source() == "field");
  clipboard::clear();
  CHECK(clipboard::empty());
  CHECK(clipboard::source() == "");
  CHECK_FALSE(fs::exists("/data/clipboard.txt"));
  CHECK(clipboard::set("", "x"));  // nothing to keep = clear
  CHECK(clipboard::empty());
}

TEST_CASE("clipboard: survives a power-off (it is a file), several lines and a first line that looks like a source") {
  TmpRoots t;
  CHECK(clipboard::set("line one\nline two\n\nline four", "game"));
  std::string raw;
  REQUIRE(fs::readFile("/data/clipboard.txt", raw));
  CHECK(raw == "game\nline one\nline two\n\nline four");
  CHECK(clipboard::get() == "line one\nline two\n\nline four");  // read back from the file: nothing is cached in memory
  CHECK(clipboard::set("no source", ""));
  CHECK(clipboard::get() == "no source");
  CHECK(clipboard::source() == "");
  CHECK(clipboard::set("text", "two words\nand a line"));  // a source is one short word
  CHECK(clipboard::source() == "twowordsandaline");
  CHECK(clipboard::get() == "text");
}

TEST_CASE("clipboard: at most 4 KB, cut at a character boundary") {
  TmpRoots t;
  CHECK(clipboard::set(std::string(5000, 'a'), "t"));
  CHECK(clipboard::get().size() == clipboard::kMaxBytes);

  // 4095 ASCII bytes, then a 2-byte and a 3-byte character: neither fits whole
  CHECK(clipboard::set(std::string(4095, 'a') + "\xC3\xA9" + "\xE2\x82\xAC", "t"));
  CHECK(clipboard::get() == std::string(4095, 'a'));
  // 4094 bytes: the 2-byte one fits exactly, the euro sign does not
  CHECK(clipboard::set(std::string(4094, 'a') + "\xC3\xA9" + "\xE2\x82\xAC", "t"));
  CHECK(clipboard::get() == std::string(4094, 'a') + "\xC3\xA9");
  // only 4-byte characters (emoji): 1024 of them fit, never a part of one
  std::string emoji;
  for (int i = 0; i < 1100; ++i) emoji += "\xF0\x9F\x9F\xA9";
  CHECK(clipboard::set(emoji, "t"));
  CHECK(clipboard::get().size() == 4096);
  CHECK(clipboard::get() == emoji.substr(0, 4096));
}

TEST_CASE("clipboard::truncateUtf8 and clean") {
  CHECK(clipboard::truncateUtf8("abc", 10) == "abc");
  CHECK(clipboard::truncateUtf8("abc", 2) == "ab");
  CHECK(clipboard::truncateUtf8("a\xE2\x82\xAC", 1) == "a");
  CHECK(clipboard::truncateUtf8("a\xE2\x82\xAC", 2) == "a");
  CHECK(clipboard::truncateUtf8("a\xE2\x82\xAC", 3) == "a");
  CHECK(clipboard::truncateUtf8("a\xE2\x82\xAC", 4) == "a\xE2\x82\xAC");
  CHECK(clipboard::truncateUtf8("\xE2\x82\xAC", 0) == "");

  CHECK(clipboard::clean("a\r\nb\tc") == "a\nb c");
  CHECK(clipboard::clean(std::string("a\0b\x01" "c\x7F", 6)) == "abc");
  CHECK(clipboard::clean("ok \xFF\xFE bad") == "ok  bad");                 // not UTF-8
  CHECK(clipboard::clean("cut \xE2\x82") == "cut ");                       // a character cut off at the end
  CHECK(clipboard::clean("\x80 stray") == " stray");                       // a continuation byte alone
  CHECK(clipboard::clean("\xC0\xAF overlong") == " overlong");             // an overlong form of '/'
  CHECK(clipboard::clean("\xED\xA0\x80 surrogate") == " surrogate");
  CHECK(clipboard::clean("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x9F\xA9") == "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x9F\xA9");
  CHECK(clipboard::clean("abcdef", 3) == "abc");
}

TEST_CASE("clipboard: the text never reaches the log, and a damaged file reads as empty or clean text") {
  TmpRoots t;
  CHECK(clipboard::set("secret-clip-4711", "field"));
  CHECK(logTail().find("secret-clip-4711") == std::string::npos);
  CHECK(fs::writeFile("/data/clipboard.txt", "no line feed at all"));
  CHECK(clipboard::empty());
  CHECK(fs::writeFile("/data/clipboard.txt", std::string("x\nbad \xFF byte and ") + std::string(6000, 'z')));
  CHECK(clipboard::get().size() == clipboard::kMaxBytes);
  CHECK(clipboard::get().substr(0, 14) == "bad  byte and ");
}

// TextField and the clipboard (no Root needed: the field only invalidates when it has one).
#include "ui/keyboard.h"

TEST_CASE("TextField: maxChars counts code points, cuts typing and paste at a character boundary") {
  TmpRoots t;
  ui::TextField f;
  int changes = 0;
  f.onChange = [&] { ++changes; };
  f.setMaxChars(5);
  f.append("ab\xC3\xA9");  // a b é: 3 characters, 4 bytes
  CHECK(f.text() == "ab\xC3\xA9");
  CHECK(ui::TextField::charCount(f.text()) == 3);
  f.append("\xC3\xA9\xC3\xA9\xC3\xA9");  // only two more fit
  CHECK(f.text() == "ab\xC3\xA9\xC3\xA9\xC3\xA9");
  CHECK(ui::TextField::charCount(f.text()) == 5);
  f.append("x");  // full: nothing changes
  CHECK(ui::TextField::charCount(f.text()) == 5);
  CHECK(changes == 2);
  f.setText("");
  CHECK_FALSE(f.paste());  // empty clipboard
  REQUIRE(clipboard::set("Somdoku 12:34", "game"));
  CHECK(f.paste());
  CHECK(f.text() == "Somdo");  // cut to the limit
  CHECK_FALSE(f.paste());      // nothing fits any more
}

TEST_CASE("TextField: a one-line field turns line feeds into spaces, a multi-line field keeps them") {
  TmpRoots t;
  REQUIRE(clipboard::set("one\ntwo", "test"));
  ui::TextField one;
  CHECK(one.paste());
  CHECK(one.text() == "one two");
  ui::TextField many;
  many.setLines(4);
  CHECK(many.paste());
  CHECK(many.text() == "one\ntwo");
  many.backspace();
  CHECK(many.text() == "one\ntw");
}
