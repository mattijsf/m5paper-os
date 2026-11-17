#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/fs.h"
#include "core/log.h"
#include "core/remote.h"

using namespace pos;

TEST_CASE("remote: parsing") {
  auto c = parseRemoteCommand(">tap 140 300");
  CHECK(c.kind == RemoteCommand::Tap);
  CHECK(c.x1 == 140);
  CHECK(c.y1 == 300);
  CHECK(parseRemoteCommand("tap 1 2").kind == RemoteCommand::Tap);  // '>' optional
  CHECK(parseRemoteCommand(">long 5 6\r").kind == RemoteCommand::Long);
  auto s = parseRemoteCommand(">swipe 400 500 100 500");
  CHECK(s.kind == RemoteCommand::Swipe);
  CHECK(s.x2 == 100);
  CHECK(parseRemoteCommand(">shot").kind == RemoteCommand::Shot);
  CHECK(parseRemoteCommand(">log").kind == RemoteCommand::Log);
  CHECK(parseRemoteCommand(">refresh").kind == RemoteCommand::Refresh);
  CHECK(parseRemoteCommand(">reboot").kind == RemoteCommand::Reboot);
  auto set = parseRemoteCommand(">set epd-timing on");
  CHECK(set.kind == RemoteCommand::Set);
  CHECK(set.key == "epd-timing");
  CHECK(set.value == "on");
}

TEST_CASE("remote: bad input is rejected with a reason") {
  for (const char* bad : {">tap 1", ">tap a b", ">tap 1 2 3", ">swipe 1 2 3", ">shot now", ">fly", ">", ">set x"}) {
    auto c = parseRemoteCommand(bad);
    CHECK_MESSAGE(c.kind == RemoteCommand::None, bad);
    CHECK_MESSAGE(!c.error.empty(), bad);
  }
}

TEST_CASE("remote: crc32 matches the standard check value") {
  const char* s = "123456789";
  CHECK(crc32(reinterpret_cast<const uint8_t*>(s), 9) == 0xCBF43926u);
}

TEST_CASE("log: ring keeps the last 50 lines, oldest first") {
  for (int i = 0; i < 60; ++i) POS_LOG("t", "line %d", i);
  std::string tail = logTail(50);
  CHECK(tail.find("line 9\n") == std::string::npos);
  CHECK(tail.find("[t] line 10\n") == 0);
  CHECK(tail.find("[t] line 59\n") != std::string::npos);
  CHECK(logTail(1) == "[t] line 59\n");
}

TEST_CASE("log: very long lines are cut, not overflowed") {
  std::string big(1000, 'x');
  POS_LOG("t", "%s", big.c_str());
  std::string last = logTail(1);
  CHECK(last.size() < 200);
  CHECK(last.back() == '\n');
}

namespace {
struct TempRoots {
  std::string base;
  TempRoots() {
    char tmpl[] = "/tmp/pos-fs-XXXXXX";
    base = mkdtemp(tmpl);
    fs::setRoots(base + "/sd", base + "/data");
  }
  ~TempRoots() {
    std::string cmd = "rm -rf '" + base + "'";
    (void)!std::system(cmd.c_str());
  }
};
}  // namespace

TEST_CASE("fs: resolve maps virtual paths and rejects others") {
  TempRoots t;
  CHECK(fs::resolve("/sd/books/a.epub") == t.base + "/sd/books/a.epub");
  CHECK(fs::resolve("/data") == t.base + "/data");
  CHECK(fs::resolve("/data/config.json") == t.base + "/data/config.json");
  CHECK(fs::resolve("/sdcard/x") == "");
  CHECK(fs::resolve("/data/../etc/passwd") == "");
  CHECK(fs::resolve("relative") == "");
  CHECK(fs::resolve("") == "");
  CHECK(fs::resolve("/sd/") == t.base + "/sd");  // one spelling: removeAll("/sd/") is the root too, and refused
  CHECK(fs::resolve("/sd//books/./a") == t.base + "/sd/books/a");
}

TEST_CASE("fs: normalize gives a path its one spelling") {
  CHECK(fs::normalize("/sd/books") == "/sd/books");
  CHECK(fs::normalize("/sd/books/") == "/sd/books");
  CHECK(fs::normalize("/sd//books/./x/") == "/sd/books/x");
  CHECK(fs::normalize("/sd/books/..") == "");
  CHECK(fs::normalize("/sd/a..b/c") == "/sd/a..b/c");  // dots inside a name are a name
  CHECK(fs::normalize("books") == "");
  CHECK(fs::normalize("/") == "");
  CHECK(fs::normalize("") == "");
}

TEST_CASE("fs: rename replaces a file, the root folders cannot be removed") {
  TempRoots t;
  REQUIRE(fs::writeFile("/sd/a.txt", "new"));
  REQUIRE(fs::writeFile("/sd/b.txt", "old"));
  CHECK(fs::rename("/sd/a.txt", "/sd/b.txt"));
  std::string s;
  CHECK(fs::readFile("/sd/b.txt", s));
  CHECK(s == "new");
  CHECK_FALSE(fs::exists("/sd/a.txt"));
  CHECK_FALSE(fs::exists("/sd/b.txt.old"));
  CHECK_FALSE(fs::rename("/sd/none", "/sd/b.txt"));
  CHECK(fs::readFile("/sd/b.txt", s));  // a failed rename leaves the destination
  CHECK(s == "new");
  CHECK(fs::isDir("/sd"));
  CHECK_FALSE(fs::isDir("/sd/b.txt"));
  CHECK_FALSE(fs::removeAll("/sd/"));
  CHECK_FALSE(fs::removeAll("/data/."));
  CHECK(fs::exists("/sd/b.txt"));
}

TEST_CASE("fs: atomic write, overwrite, read, list, mkdirs") {
  TempRoots t;
  CHECK_FALSE(fs::exists("/data/a/b.txt"));
  CHECK(fs::writeFile("/data/a/b.txt", "one"));  // creates the parent folder
  CHECK(fs::writeFile("/data/a/b.txt", "second"));
  std::string s;
  CHECK(fs::readFile("/data/a/b.txt", s));
  CHECK(s == "second");
  CHECK_FALSE(fs::exists("/data/a/b.txt.tmp"));  // no temp file left behind
  CHECK(fs::writeFile("/data/a/empty", ""));
  CHECK(fs::readFile("/data/a/empty", s));
  CHECK(s.empty());

  CHECK(fs::mkdirs("/sd/x/y/z"));
  CHECK(fs::exists("/sd/x/y/z"));
  CHECK(fs::mkdirs("/sd/x/y/z"));  // already there

  std::vector<fs::DirEntry> list;
  REQUIRE(fs::listDir("/data/a", list));
  REQUIRE(list.size() == 2);
  CHECK(list[0].name == "b.txt");  // sorted
  CHECK(list[0].size == 6);
  CHECK_FALSE(list[0].isDir);
  CHECK(list[1].name == "empty");
  REQUIRE(fs::listDir("/sd/x", list));
  REQUIRE(list.size() == 1);
  CHECK(list[0].isDir);

  CHECK_FALSE(fs::readFile("/data/none", s));
  CHECK_FALSE(fs::listDir("/data/none", list));
  CHECK_FALSE(fs::writeFile("/etc/x", "no"));
}
