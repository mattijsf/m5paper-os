#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>

#include "core/config.h"
#include "core/fs.h"
#include "core/secrets.h"
#include "core/timefmt.h"

using namespace pos;

namespace {
struct TempRoots {
  std::string dir;
  TempRoots() {
    char tmpl[] = "/tmp/pos-cfg-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~TempRoots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};
}  // namespace

TEST_CASE("config: defaults when there is no file") {
  TempRoots t;
  Config c;
  CHECK_FALSE(c.load());
  CHECK(c.display.ghostLimit == doctest::Approx(8.0f));
  CHECK(c.display.settleMs == 600);
  CHECK(c.display.idleCleanMs == 2000);
  CHECK(c.power.autoOffMin == 15);
  CHECK(c.net.timezone == "CET-1CEST,M3.5.0,M10.5.0/3");
  CHECK(c.reader.fontSize == "M");
  CHECK(c.reader.lineSpacing == doctest::Approx(1.35f));
  CHECK(c.launcher.resumeLastApp);
}

TEST_CASE("config: save and load round trip") {
  TempRoots t;
  Config a;
  a.display.ghostLimit = 11;
  a.display.settleMs = 1000;
  a.power.autoOffMin = 0;
  a.reader.fontSize = "XL";
  a.reader.margins = "wide";
  a.reader.lineSpacing = 1.5f;
  a.ha.baseUrl = "http://192.168.1.5:8123";
  a.launcher.resumeLastApp = false;
  a.net.timezone = "UTC0";
  REQUIRE(a.save());
  Config b;
  REQUIRE(b.load());
  CHECK(b.display.ghostLimit == doctest::Approx(11.0f));
  CHECK(b.display.settleMs == 1000);
  CHECK(b.power.autoOffMin == 0);
  CHECK(b.reader.fontSize == "XL");
  CHECK(b.reader.margins == "wide");
  CHECK(b.reader.lineSpacing == doctest::Approx(1.5f));
  CHECK(b.ha.baseUrl == "http://192.168.1.5:8123");
  CHECK_FALSE(b.launcher.resumeLastApp);
  CHECK(b.net.timezone == "UTC0");
}

TEST_CASE("config: missing keys, wrong types and out-of-range values never crash") {
  TempRoots t;
  fs::writeFile("/data/config.json", R"({
    "display": {"ghostLimit": 99, "settleMs": "fast", "idleCleanMs": 1},
    "power": {"autoOffMin": -5},
    "reader": {"fontSize": "XXL", "margins": 3, "lineSpacing": 9.0},
    "ha": {"baseUrl": ""},
    "launcher": {"resumeLastApp": "yes"},
    "unknown": {"x": 1}
  })");
  Config c;
  REQUIRE(c.load());
  CHECK(c.display.ghostLimit == doctest::Approx(16.0f));  // clamped
  CHECK(c.display.settleMs == 600);                       // wrong type: default
  CHECK(c.display.idleCleanMs == 500);                    // clamped
  CHECK(c.power.autoOffMin == 0);
  CHECK(c.reader.fontSize == "M");                        // not one of S M L XL
  CHECK(c.reader.margins == "normal");
  CHECK(c.reader.lineSpacing == doctest::Approx(1.8f));
  CHECK(c.ha.baseUrl == "http://homeassistant.local:8123");  // empty: default
  CHECK(c.launcher.resumeLastApp);

  fs::writeFile("/data/config.json", "{ this is not json");
  Config d;
  CHECK_FALSE(d.load());
  CHECK(d.display.settleMs == 600);
  fs::writeFile("/data/config.json", "[1,2,3]");
  CHECK_FALSE(d.load());
}

TEST_CASE("config: WhatsApp favourites load, skip bad entries, cap at 8 and survive a save") {
  TempRoots t;
  std::string json = R"({"inbox":{"whatsapp":{"favorites":[
    {"name":"Family","chat":"120363000000000001@g.us"},
    {"name":"","chat":"31600000001@s.whatsapp.net"},
    {"name":"No chat"},
    {"name":42,"chat":"31600000002@s.whatsapp.net"},
    "not an object",
    {"name":"Bram","chat":"31600000002@s.whatsapp.net"})";
  for (int i = 0; i < 12; ++i) json += R"(,{"name":"F)" + std::to_string(i) + R"(","chat":"3160000010)" + std::to_string(i % 10) + R"(@s.whatsapp.net"})";
  json += "]}}}";
  REQUIRE(fs::writeFile("/data/config.json", json));
  Config c;
  REQUIRE(c.load());
  REQUIRE(c.inbox.whatsappFavorites.size() == 8);
  CHECK(c.inbox.whatsappFavorites[0].name == "Family");
  CHECK(c.inbox.whatsappFavorites[0].chat == "120363000000000001@g.us");
  CHECK(c.inbox.whatsappFavorites[1].name == "Bram");
  CHECK(c.inbox.whatsappFavorites[2].name == "F0");
  c.display.settleMs = 700;  // Settings saves the whole file: the favourites must come back
  REQUIRE(c.save());
  Config d;
  REQUIRE(d.load());
  REQUIRE(d.inbox.whatsappFavorites.size() == 8);
  CHECK(d.inbox.whatsappFavorites[1].chat == "31600000002@s.whatsapp.net");
  CHECK(d.display.settleMs == 700);
  Config e;  // no favourites: none written
  e.reset();
  REQUIRE(e.save());
  std::string text;
  REQUIRE(fs::readFile("/data/config.json", text));
  CHECK(text.find("favorites") == std::string::npos);
}

TEST_CASE("secrets: parsing") {
  TempRoots t;
  fs::writeFile("/sd/paperos_config",
                "# comment\r\n"
                "HA_TOKEN = abc=def==  \r\n"
                "\n"
                "   \n"
                "NOEQUALS\n"
                "=novalue\n"
                "HA_URL=http://x:8123\n"
                "EMPTY=\n"
                "# WIFI_SSID=commented\n"
                "WIFI_SSID=my net");
  Secrets s;
  REQUIRE(s.load());
  CHECK(s.fileFound());
  CHECK(s.get("HA_TOKEN") == "abc=def==");  // split at the first '=', trimmed
  CHECK(s.get("HA_URL") == "http://x:8123");
  CHECK(s.get("WIFI_SSID") == "my net");  // last line without a newline
  CHECK(s.present("HA_TOKEN"));
  CHECK_FALSE(s.present("EMPTY"));         // empty value = not configured
  CHECK_FALSE(s.present("NOEQUALS"));
  CHECK_FALSE(s.present("MISSING"));
  CHECK(s.get("MISSING").empty());
  auto keys = s.keys();
  CHECK(keys == std::vector<std::string>{"HA_TOKEN", "HA_URL", "EMPTY", "WIFI_SSID"});
}

TEST_CASE("secrets: no file means nothing configured") {
  TempRoots t;
  Secrets s;
  CHECK_FALSE(s.load());
  CHECK_FALSE(s.fileFound());
  CHECK_FALSE(s.present("HA_TOKEN"));
  CHECK(s.keys().empty());
}

TEST_CASE("timefmt: local time follows the configured zone") {
  applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  struct tm tmv;
  REQUIRE(localTime(1768480440, tmv));  // 2026-01-15 12:34:00 UTC
  CHECK(tmv.tm_hour == 13);
  CHECK(tmv.tm_min == 34);
  REQUIRE(localTime(1784118840, tmv));  // 2026-07-15 12:34:00 UTC, summer time
  CHECK(tmv.tm_hour == 14);
  applyTimezone("UTC0");
  REQUIRE(localTime(1768480440, tmv));
  CHECK(tmv.tm_hour == 12);
  CHECK_FALSE(localTime(0, tmv));  // the RTC was never set
  applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
}

TEST_CASE("fs::removeAll deletes files and folders, never the roots") {
  TempRoots t;
  fs::writeFile("/data/cache/a/x.txt", "1");
  fs::writeFile("/data/cache/b.txt", "2");
  CHECK(fs::removeAll("/data/cache"));
  CHECK_FALSE(fs::exists("/data/cache"));
  CHECK(fs::removeAll("/data/cache"));  // already gone
  fs::writeFile("/data/keep.txt", "k");
  CHECK_FALSE(fs::removeAll("/data"));  // the root itself is protected
  CHECK(fs::exists("/data/keep.txt"));
  CHECK_FALSE(fs::removeAll("/etc"));
}

TEST_CASE("timefmt: wall time is formatted in the configured zone (CET/CEST), the clock itself stays UTC") {
  applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  struct tm tmv;
  // 2026-11-15 15:51:00 UTC = 16:51 in Amsterdam (the case seen on the device)
  REQUIRE(localTime(1794757860, tmv));
  CHECK(tmv.tm_hour == 16);
  CHECK(tmv.tm_min == 51);
  // summer: 2026-07-15 12:34 UTC = 14:34 CEST
  REQUIRE(localTime(1784118840, tmv));
  CHECK(tmv.tm_hour == 14);
  // the changeover: 2026-03-29 00:59 UTC is 01:59 CET, 01:00 UTC is 03:00 CEST
  REQUIRE(localTime(1774745940, tmv));
  CHECK(tmv.tm_hour == 1);
  REQUIRE(localTime(1774746000, tmv));
  CHECK(tmv.tm_hour == 3);
  // another configured zone is honoured and applyTimezone can switch back
  applyTimezone("UTC0");
  REQUIRE(localTime(1794757860, tmv));
  CHECK(tmv.tm_hour == 15);
  applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
}
