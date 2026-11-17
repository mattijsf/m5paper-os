#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "core/remote.h"

using namespace pos;

TEST_CASE("remote: put/ls/rm parsing, paths with spaces") {
  auto c = parseRemoteCommand(">put /sd/books/Alice's Adventures - Lewis Carroll.epub 189249 1a2b3c4d");
  CHECK(c.kind == RemoteCommand::Put);
  CHECK(c.path == "/sd/books/Alice's Adventures - Lewis Carroll.epub");
  CHECK(c.size == 189249);
  CHECK(c.crc == 0x1a2b3c4du);
  CHECK(parseRemoteCommand(">put /sd/books/a.txt 0 0").kind == RemoteCommand::Put);
  CHECK(parseRemoteCommand(">put /sd/books/a.txt 12").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">put /sd/books/a.txt x y").kind == RemoteCommand::None);
  auto ls = parseRemoteCommand(">ls /sd/books/My Folder");
  CHECK(ls.kind == RemoteCommand::Ls);
  CHECK(ls.path == "/sd/books/My Folder");
  CHECK(parseRemoteCommand(">rm /sd/books/x.epub").kind == RemoteCommand::Rm);
  CHECK(parseRemoteCommand(">ls").kind == RemoteCommand::None);
}

TEST_CASE("remote: only /sd/books and /sd/paperos, never paperos_config or ..") {
  CHECK(remotePathAllowed("/sd/books/a.epub"));
  CHECK(remotePathAllowed("/sd/books/.paperos/ab12/meta.json"));
  CHECK(remotePathAllowed("/sd/paperos/x.json"));
  CHECK(remotePathAllowed("/sd/books"));
  CHECK_FALSE(remotePathAllowed("/sd/paperos_config"));
  CHECK_FALSE(remotePathAllowed("/sd/books/paperos_config"));
  CHECK_FALSE(remotePathAllowed("/sd/paperos/paperos_config"));
  CHECK_FALSE(remotePathAllowed("/sd/books/../paperos_config"));
  CHECK_FALSE(remotePathAllowed("/data/config.json"));
  CHECK_FALSE(remotePathAllowed("/sd/other/x"));
  CHECK_FALSE(remotePathAllowed("/sd/bookshelf/x"));
  CHECK_FALSE(remotePathAllowed("/sd/books\\..\\x"));
  CHECK_FALSE(remotePathAllowed("/sd/paperos/PaperOS_Config"));  // FAT: the same file
  CHECK_FALSE(remotePathAllowed("sd/books/a.epub"));
}

TEST_CASE("crc32 chains") {
  const char* s = "123456789";
  const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
  CHECK(crc32(p, 9) == 0xCBF43926u);
  CHECK(crc32(p + 4, 5, crc32(p, 4)) == 0xCBF43926u);
}

namespace {

class FakeDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect, Refresh) override {}
  bool busy() override { return false; }
  void wait() override {}
};
class FakeInput : public hal::Input {
 public:
  bool poll(hal::TouchEvent&) override { return false; }
};
class FakeClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return 0; }
  void set(int64_t) override {}
  uint32_t ms = 1000;
};
class FakeNet : public hal::Net {
 public:
  bool wifiConnect(const std::string&, const std::string&, uint32_t, const std::string& = "", int = 0) override { return false; }
  void wifiOff() override {}
  bool wifiConnected() override { return false; }
  int rssi() override { return 0; }
  hal::WifiLink link() override { return {}; }
  std::vector<hal::WifiNetwork> scan() override { return {}; }
  hal::HttpResponse request(const hal::HttpRequest&) override { return {}; }
};
class FakePower : public hal::Power {
 public:
  int batteryMv() override { return 3900; }
  int batteryPercent() override { return 70; }
  bool charging() override { return false; }
  hal::Wake lightSleep(uint32_t) override { return hal::Wake::Timer; }
  void powerOff(uint32_t) override {}
};
class Host : public RemoteHost {
 public:
  void inject(const hal::TouchEvent&) override {}
  void reply(const void* d, size_t n) override {
    out.append(static_cast<const char*>(d), n);
    if (room != static_cast<size_t>(-1)) room -= n;
  }
  size_t writable() override { return room; }
  void replyLeaving(bool on) override { leaving.push_back(on); }
  void reboot() override {}
  std::string out;
  size_t room = static_cast<size_t>(-1);  // bytes reply() takes before writable() says 0 (a full USB buffer)
  std::vector<bool> leaving;
  bool has(const std::string& s) const { return out.find(s) != std::string::npos; }
};

struct Env {
  FakeDisplay display;
  FakeInput input;
  FakeClock clock;
  FakeNet net;
  FakePower power;
  hal::Platform platform{display, input, clock, net, power, "test"};
  Os os{platform};
  Host host;
  Remote remote{os, clock, host};
  std::string dir;
  Env() {
    char tmpl[] = "/tmp/pos-rf-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~Env() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
  void put(const std::string& path, const std::string& data, uint32_t crc, size_t chunk = 4096) {
    char line[512];
    std::snprintf(line, sizeof line, ">put %s %zu %08x", path.c_str(), data.size(), crc);
    remote.handleLine(line);
    for (size_t i = 0; i < data.size(); i += chunk)
      remote.feedRaw(reinterpret_cast<const uint8_t*>(data.data()) + i, std::min(chunk, data.size() - i));
  }
};

std::string blob(size_t n) {
  std::string s(n, 'x');
  unsigned x = 12345;
  for (auto& c : s) {
    x = x * 1664525u + 1013904223u;
    c = static_cast<char>(x >> 24);
  }
  return s;
}

}  // namespace

TEST_CASE("remote put: chunks are acknowledged, the crc is checked, the file appears only when complete") {
  Env e;
  const std::string data = blob(10000);
  const uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  e.remote.handleLine(">put /sd/books/My Book - Someone.txt 10000 " + std::string([&] { char b[16]; std::snprintf(b, sizeof b, "%08x", crc); return std::string(b); }()));
  CHECK(e.host.has("#put go"));
  CHECK(e.remote.wantsRaw());
  e.remote.feedRaw(reinterpret_cast<const uint8_t*>(data.data()), 4096);
  CHECK(e.host.has("#ack 4096"));
  CHECK_FALSE(fs::exists("/sd/books/My Book - Someone.txt"));  // still a .part file
  CHECK(fs::exists("/sd/books/My Book - Someone.txt.part"));
  e.remote.feedRaw(reinterpret_cast<const uint8_t*>(data.data()) + 4096, 4096);
  CHECK(e.host.has("#ack 8192"));
  e.remote.feedRaw(reinterpret_cast<const uint8_t*>(data.data()) + 8192, 1808);
  CHECK(e.host.has("#ok"));
  CHECK_FALSE(e.remote.wantsRaw());
  std::string back;
  REQUIRE(fs::readFile("/sd/books/My Book - Someone.txt", back));
  CHECK(back == data);
  CHECK_FALSE(fs::exists("/sd/books/My Book - Someone.txt.part"));
}

TEST_CASE("remote put: arbitrary chunking, an empty file, overwriting") {
  Env e;
  const std::string data = blob(5000);
  const uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  e.put("/sd/books/a.bin", data, crc, 7);  // 7-byte pieces
  CHECK(e.host.has("#ok"));
  std::string back;
  REQUIRE(fs::readFile("/sd/books/a.bin", back));
  CHECK(back == data);

  Env f;
  f.put("/sd/books/empty.txt", "", 0);
  CHECK(f.host.has("#ok"));
  CHECK(fs::exists("/sd/books/empty.txt"));

  e.host.out.clear();
  const std::string data2 = blob(100);
  e.put("/sd/books/a.bin", data2, crc32(reinterpret_cast<const uint8_t*>(data2.data()), data2.size()));
  fs::readFile("/sd/books/a.bin", back);
  CHECK(back == data2);  // replaced
}

TEST_CASE("remote put: wrong crc, forbidden paths and too much data are refused and leave nothing behind") {
  Env e;
  const std::string data = blob(3000);
  e.put("/sd/books/bad.txt", data, 0xDEADBEEF);
  CHECK(e.host.has("#err crc mismatch"));
  CHECK_FALSE(fs::exists("/sd/books/bad.txt"));
  CHECK_FALSE(fs::exists("/sd/books/bad.txt.part"));
  CHECK_FALSE(e.remote.wantsRaw());

  Env f;
  f.remote.handleLine(">put /sd/paperos_config 10 00000000");
  CHECK(f.host.has("#err path not allowed"));
  f.host.out.clear();
  f.remote.handleLine(">put /sd/books/../paperos_config 10 00000000");
  CHECK(f.host.has("#err path not allowed"));
  f.host.out.clear();
  f.remote.handleLine(">put /sd/books/ 10 00000000");
  CHECK(f.host.has("#err path not allowed"));
  CHECK_FALSE(f.remote.wantsRaw());
}

TEST_CASE("remote put: an upload that stalls is dropped") {
  Env e;
  e.remote.handleLine(">put /sd/books/slow.bin 100000 00000000");
  e.remote.feedRaw(reinterpret_cast<const uint8_t*>("abc"), 3);
  e.clock.ms += 9000;
  e.remote.tick();
  CHECK(e.host.has("#err upload timed out"));
  CHECK_FALSE(e.remote.wantsRaw());
  CHECK_FALSE(fs::exists("/sd/books/slow.bin.part"));
}

TEST_CASE("remote ls and rm") {
  Env e;
  fs::writeFile("/sd/books/a.txt", "12345");
  fs::writeFile("/sd/books/b b.epub", "xx");
  fs::writeFile("/sd/books/.paperos/id1/meta.json", "{}");
  e.remote.handleLine(">ls /sd/books");
  CHECK(e.host.has("a.txt\t5\n"));
  CHECK(e.host.has("b b.epub\t2\n"));
  CHECK(e.host.has(".paperos\t-\n"));
  e.host.out.clear();
  e.remote.handleLine(">ls /sd/books/nope");
  CHECK(e.host.has("#err no such folder"));
  e.host.out.clear();
  e.remote.handleLine(">ls /data");
  CHECK(e.host.has("#err path not allowed"));
  e.host.out.clear();
  e.remote.handleLine(">rm /sd/books/a.txt");
  CHECK(e.host.has("#ok"));
  CHECK_FALSE(fs::exists("/sd/books/a.txt"));
  e.host.out.clear();
  e.remote.handleLine(">rm /sd/books/.paperos");  // a folder with content
  CHECK(e.host.has("#ok"));
  CHECK_FALSE(fs::exists("/sd/books/.paperos/id1/meta.json"));
  e.host.out.clear();
  for (const char* spelling : {"/sd/books", "/sd/books/", "/sd/books/.", "/sd//books", "/sd/paperos/"}) {  // the folders themselves are protected
    e.host.out.clear();
    e.remote.handleLine(std::string(">rm ") + spelling);
    CHECK(e.host.has("#err path not allowed"));
  }
  CHECK(fs::isDir("/sd/books"));
  e.host.out.clear();
  e.remote.handleLine(">rm /sd/paperos_config");
  CHECK(e.host.has("#err path not allowed"));
}

TEST_CASE("remote: feed splits lines and upload bytes itself, in any chunking") {
  Env e;
  const std::string data = "hello paper";
  const uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
  char head[96];
  std::snprintf(head, sizeof head, ">put /sd/books/f.txt %zu %08x\n", data.size(), static_cast<unsigned>(crc));
  // One buffer: the command, the file's bytes, and the next command right behind them.
  const std::string all = std::string(head) + data + ">ls /sd/books\n";
  for (size_t chunk : {all.size(), size_t{1}, size_t{7}}) {
    fs::removeAll("/sd/books/f.txt");
    e.host.out.clear();
    for (size_t i = 0; i < all.size(); i += chunk)
      e.remote.feed(reinterpret_cast<const uint8_t*>(all.data()) + i, std::min(chunk, all.size() - i));
    std::string text;
    CHECK(fs::readFile("/sd/books/f.txt", text));
    CHECK(text == data);
    CHECK(e.host.has("#ls "));  // the command after the upload was not swallowed
    CHECK(e.host.has("f.txt\t11\n"));
  }
}

TEST_CASE("remote: a line that is too long is refused, not run cut short") {
  Env e;
  REQUIRE(fs::writeFile("/sd/books/keep/a.txt", "x"));
  // Cut at the limit this would be ">rm /sd/books/keep".
  std::string line = ">rm /sd/books/keep";
  line.resize(Remote::kMaxLine, ' ');
  line += "/a.txt.old\n>status\n";
  e.remote.feed(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  CHECK(e.host.has("#err line too long"));
  CHECK(fs::exists("/sd/books/keep/a.txt"));
  CHECK(e.host.has("#status "));  // the next line is read as usual
}

TEST_CASE("remote: poweroff needs 1..3600 seconds, never 0") {
  auto c = parseRemoteCommand(">poweroff 20");
  CHECK(c.kind == RemoteCommand::PowerOff);
  CHECK(c.x1 == 20);
  CHECK(parseRemoteCommand(">poweroff 0").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff 3601").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff -5").kind == RemoteCommand::None);
  auto two = parseRemoteCommand(">poweroff 20 30");  // off 20 s, scheduled refresh, off 30 s more
  CHECK(two.kind == RemoteCommand::PowerOff);
  CHECK(two.x1 == 20);
  CHECK(two.y1 == 30);
  CHECK(parseRemoteCommand(">poweroff 20").y1 == 0);
  CHECK(parseRemoteCommand(">poweroff 20 0").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff 20 x").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff 20 30 40").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff 20 3601").kind == RemoteCommand::None);
  CHECK(parseRemoteCommand(">poweroff 5 6").kind == RemoteCommand::PowerOff);  // since P7-5: a second power-off after the refresh
}

TEST_CASE("remote: state takes no arguments") {
  CHECK(parseRemoteCommand(">state").kind == RemoteCommand::State);
  CHECK(parseRemoteCommand(">state now").kind == RemoteCommand::None);
}

TEST_CASE("remote: a reply that leaves in pieces tells the host from its first byte to its last") {
  // The device holds its log lines back between the two calls: a line printed between two pieces of a frame dump landed
  // inside the pixels and the host never saw "#ok".
  Env e;
  e.host.room = 4;
  e.remote.handleLine(">log");
  CHECK(e.host.out.size() == 4);
  REQUIRE(e.host.leaving.size() == 1);
  CHECK(e.host.leaving[0] == true);
  e.remote.tick();  // still no room: nothing changes
  CHECK(e.host.out.size() == 4);
  CHECK(e.host.leaving.size() == 1);
  e.host.room = 3;
  e.remote.tick();
  CHECK(e.host.out.size() == 7);
  CHECK(e.host.leaving.size() == 1);
  e.host.room = static_cast<size_t>(-1);
  e.remote.tick();
  CHECK(e.host.out.substr(e.host.out.size() - 4) == "#ok\n");
  REQUIRE(e.host.leaving.size() == 2);
  CHECK(e.host.leaving[1] == false);
  CHECK_FALSE(e.remote.busy());
  // A reply that fits at once: on and off again in the same call.
  e.remote.handleLine(">state");
  REQUIRE(e.host.leaving.size() == 4);
  CHECK(e.host.leaving[2] == true);
  CHECK(e.host.leaving[3] == false);
}
