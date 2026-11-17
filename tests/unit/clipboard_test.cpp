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

TEST_CASE("clipboard::clean") {
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

TEST_CASE("clipboard: the sync pushes copies but never what came from the phone; pull without a sync answers at once") {
  TmpRoots t;
  std::vector<std::string> pushed;
  bool active = true;
  clipboard::Sync s;
  s.active = [&] { return active; };
  s.push = [&](const std::string& text) { pushed.push_back(text); };
  s.pull = [&](std::function<void(bool)> done) {
    clipboard::set("from the phone", clipboard::kFromPhone);
    done(true);
  };
  clipboard::setSync(s);
  CHECK(clipboard::set("score", "somdoku"));
  REQUIRE(pushed.size() == 1);
  CHECK(pushed[0] == "score");
  bool changed = false;
  clipboard::pull([&](bool c) { changed = c; });
  CHECK(changed);
  CHECK(clipboard::get() == "from the phone");
  CHECK(clipboard::source() == clipboard::kFromPhone);
  CHECK(pushed.size() == 1);  // the phone's own text does not go back
  active = false;  // unpaired or Settings switched it off
  CHECK(clipboard::set("local", "field"));
  CHECK(pushed.size() == 1);
  bool called = false;
  clipboard::pull([&](bool c) {
    called = true;
    changed = c;
  });
  CHECK(called);
  CHECK_FALSE(changed);
  clipboard::setSync({});
  CHECK_FALSE(clipboard::syncing());
}

TEST_CASE("clipboard: a copy the phone has not got is resent once Wi-Fi is up, unless something newer replaced it or it is too old") {
  TmpRoots t;
  std::string text;
  int64_t ts = 0;
  CHECK_FALSE(clipboard::unsent(1000, 86400, text, ts));  // nothing owed
  CHECK(clipboard::set("score", "somdoku"));
  clipboard::markUnsent(1000);  // the push failed: no Wi-Fi
  REQUIRE(clipboard::unsent(2000, 86400, text, ts));
  CHECK(text == "score");
  CHECK(ts == 1000);  // the copy's time, not the resend's
  clipboard::markSent(1000);  // the resend arrived
  CHECK_FALSE(clipboard::unsent(2000, 86400, text, ts));

  clipboard::markUnsent(1000);
  clipboard::markSent(900);  // an earlier push answering late does not settle a later copy
  CHECK(clipboard::unsent(2000, 86400, text, ts));
  CHECK(clipboard::set("newer", "field"));  // a new copy is pushed itself: the old one is no longer owed
  CHECK_FALSE(clipboard::unsent(2000, 86400, text, ts));

  clipboard::markUnsent(1000);
  CHECK(clipboard::set("from the phone", clipboard::kFromPhone));
  CHECK_FALSE(clipboard::unsent(2000, 86400, text, ts));  // the phone's text is newer, and never goes back

  CHECK(clipboard::set("late", "field"));
  clipboard::markUnsent(1000);
  CHECK_FALSE(clipboard::unsent(1000 + 86401, 86400, text, ts));  // older than a day: the phone would drop it
  CHECK_FALSE(clipboard::unsent(1000, 86400, text, ts));          // and the marker is gone

  clipboard::markUnsent(1000);
  clipboard::clear();
  CHECK_FALSE(clipboard::unsent(1000, 86400, text, ts));
  CHECK(fs::writeFile("/data/clipboard_unsent", "12x"));  // damaged: dropped
  CHECK(clipboard::set("x", "field"));
  CHECK(fs::writeFile("/data/clipboard_unsent", "12x"));
  CHECK_FALSE(clipboard::unsent(1000, 86400, text, ts));
  CHECK_FALSE(fs::exists("/data/clipboard_unsent"));
}
