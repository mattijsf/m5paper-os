#include <doctest.h>

#include <cstdlib>
#include <functional>
#include <string>
#include <unistd.h>

#include "core/display_manager.h"
#include "core/fs.h"
#include "core/http_fixture.h"
#include "core/net.h"
#include "core/wifi.h"

using namespace pos;

namespace {

struct TempRoots {
  std::string dir;
  TempRoots() {
    char tmpl[] = "/tmp/pos-wifi-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~TempRoots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

Secrets secretsFrom(const std::string& text) {
  fs::writeFile("/sd/paperos_config", text);
  Secrets s;
  s.load();
  return s;
}

}  // namespace

TEST_CASE("secrets: several Wi-Fi networks, odd SSIDs, open networks") {
  TempRoots t;
  Secrets s = secretsFrom(
      "HA_TOKEN=abc\n"
      "WIFI_SSID=Home Net\n"
      "WIFI_PASSWORD=pa=ss word\n"            // '=' inside the value
      "WIFI_SSID_2=My=Net/5.0 GHz.guest\r\n"  // '=', '/', '.', spaces in the SSID
      "WIFI_PASSWORD_2=second\r\n"
      "WIFI_SSID_3 = Cafe Free  \n"           // no password key: open; spaces around key and value
      "WIFI_SSID_4=Blank\n"
      "WIFI_PASSWORD_4=\n"                    // empty password: open
      "WIFI_PASSWORD_5=orphan\n"              // a password without an SSID: ignored
      "WIFI_SSID_8=Eighth\nWIFI_PASSWORD_8=eight\n"
      "WIFI_SSID_9=Ninth\nWIFI_PASSWORD_9=nine\n");  // only up to _8
  auto n = s.wifiNetworks();
  REQUIRE(n.size() == 5);
  CHECK(n[0].ssid == "Home Net");
  CHECK(n[0].password == "pa=ss word");
  CHECK_FALSE(n[0].open());
  CHECK(n[0].fromCard);
  CHECK(n[1].ssid == "My=Net/5.0 GHz.guest");
  CHECK(n[1].password == "second");
  CHECK(n[2].ssid == "Cafe Free");
  CHECK(n[2].open());
  CHECK(n[3].ssid == "Blank");
  CHECK(n[3].open());
  CHECK(n[4].ssid == "Eighth");
  CHECK(s.valueLength("HA_TOKEN") == 3);  // length only
}

TEST_CASE("secrets: no Wi-Fi keys, no file") {
  TempRoots t;
  Secrets none;
  CHECK(none.wifiNetworks().empty());
  CHECK(secretsFrom("HA_TOKEN=x\n").wifiNetworks().empty());
}

TEST_CASE("WifiStore: round trip, add replaces, forget, last and memory") {
  TempRoots t;
  WifiStore a;
  a.add("Phone", "p1");
  a.add("Phone", "p2");  // same SSID: replaced
  a.add("Caf\xC3\xA9 5G", "");
  REQUIRE(a.networks().size() == 2);
  CHECK(a.networks()[0].password == "p2");
  a.remember("Phone", "AA:BB:CC:DD:EE:FF", 11);
  REQUIRE(a.save());

  WifiStore b;
  REQUIRE(b.load());
  REQUIRE(b.networks().size() == 2);
  CHECK(b.networks()[1].ssid == "Caf\xC3\xA9 5G");
  CHECK(b.networks()[1].open());
  CHECK(b.last() == "Phone");
  auto m = b.memoryOf("Phone");
  REQUIRE(m.has_value());
  CHECK(m->bssid == "AA:BB:CC:DD:EE:FF");
  CHECK(m->channel == 11);
  CHECK_FALSE(b.memoryOf("Nope").has_value());

  CHECK(b.forget("Phone"));
  CHECK_FALSE(b.forget("Phone"));
  CHECK(b.last().empty());  // forgetting the last network clears it
  CHECK_FALSE(b.memoryOf("Phone").has_value());

  fs::writeFile("/data/wifi.json", "{ broken");
  WifiStore c;
  CHECK_FALSE(c.load());
  CHECK(c.networks().empty());
}

TEST_CASE("knownNetworks: card first, device after, the card wins duplicates") {
  TempRoots t;
  Secrets card = secretsFrom("WIFI_SSID=Home\nWIFI_PASSWORD=card\nWIFI_SSID_2=Work\nWIFI_PASSWORD_2=w\n");
  WifiStore dev;
  dev.add("Home", "device-password");  // same SSID as the card: dropped
  dev.add("Phone", "ph");
  auto k = knownNetworks(card, dev);
  REQUIRE(k.size() == 3);
  CHECK(k[0].ssid == "Home");
  CHECK(k[0].password == "card");
  CHECK(k[0].fromCard);
  CHECK(k[1].ssid == "Work");
  CHECK(k[2].ssid == "Phone");
  CHECK_FALSE(k[2].fromCard);
}

TEST_CASE("selection: the last network first, else the strongest known one") {
  std::vector<WifiCred> known(3);
  known[0].ssid = "Home";
  known[0].password = "h";
  known[1].ssid = "Work";
  known[1].password = "w";
  known[2].ssid = "Cafe";  // open
  CHECK(fastCandidate(known, "Work")->ssid == "Work");
  CHECK_FALSE(fastCandidate(known, "Gone").has_value());  // last network no longer known
  CHECK_FALSE(fastCandidate(known, "").has_value());      // nothing worked yet

  std::vector<hal::WifiNetwork> scan = {
      {"Stranger", -30, true, 1, "01"}, {"Work", -70, true, 6, "02"},   {"Home", -80, true, 11, "03"},
      {"Home", -55, true, 1, "04"},     {"Cafe", -40, false, 9, "05"}, {"Cafe", -35, true, 2, "06"},
  };
  auto p = strongestKnown(known, scan);
  REQUIRE(p.has_value());
  CHECK(p->cred.ssid == "Cafe");  // -40 dBm: the open "Cafe" entry; the secured "Cafe" at -35 does not match an open network
  CHECK(p->ap.bssid == "05");
  scan.erase(scan.begin() + 4);
  p = strongestKnown(known, scan);
  CHECK(p->cred.ssid == "Home");  // the stronger of the two Home access points
  CHECK(p->ap.bssid == "04");
  CHECK(p->ap.channel == 1);
  CHECK_FALSE(strongestKnown(known, {{"Stranger", -30, true, 1, "x"}}).has_value());
  CHECK_FALSE(strongestKnown({}, scan).has_value());
  CHECK_FALSE(strongestKnown(known, {}).has_value());
}

TEST_CASE("scanSummary lists SSIDs with signal and channel, strongest first, no passwords") {
  std::vector<hal::WifiNetwork> scan = {{"B", -70, true, 1, ""}, {"A", -40, true, 6, ""}, {"", -90, true, 11, ""}};
  CHECK(scanSummary(scan) == "A -40 dBm ch6, B -70 dBm ch1, (hidden) -90 dBm ch11");
  CHECK(scanSummary({}) == "no networks found");
  CHECK(scanSummary(scan, 1) == "A -40 dBm ch6, +2 more");
}

// ---- NetService with a scripted radio

namespace {

class ScriptedNet : public hal::Net {
 public:
  struct Call {
    std::string ssid, bssid;
    int channel;
    uint32_t timeoutMs;
  };
  std::vector<hal::WifiNetwork> world;
  std::function<bool(const Call&)> accepts = [](const Call&) { return true; };
  std::vector<Call> connects;
  int scans = 0, offs = 0, requests = 0;
  bool up = false;
  hal::WifiLink cur;
  int64_t time = 1800000000;

  bool wifiConnect(const std::string& ssid, const std::string& pass, uint32_t timeoutMs, const std::string& bssid, int channel) override {
    (void)pass;
    Call c{ssid, bssid, channel, timeoutMs};
    connects.push_back(c);
    if (!accepts(c)) return false;
    up = true;
    cur = hal::WifiLink{};
    cur.connected = true;
    cur.ssid = ssid;
    cur.bssid = bssid.empty() ? "99:99:99:99:99:99" : bssid;
    cur.channel = channel ? channel : 6;
    cur.ip = "10.0.0.2";
    return true;
  }
  void wifiOff() override {
    up = false;
    cur = hal::WifiLink{};
    ++offs;
  }
  bool wifiConnected() override { return up; }
  int rssi() override { return -50; }
  hal::WifiLink link() override { return cur; }
  std::vector<hal::WifiNetwork> scan() override {
    ++scans;
    return world;
  }
  hal::HttpResponse request(const hal::HttpRequest& r) override {
    ++requests;
    hal::HttpResponse out;
    out.status = 200;
    out.body = r.url;
    return out;
  }
  int64_t syncTime(uint32_t) override { return time; }
};

class StubDisplay : public hal::Display {
 public:
  void push(const Canvas&, Rect, Refresh) override {}
  bool busy() override { return false; }
  void wait() override {}
};
class StubInput : public hal::Input {
 public:
  bool poll(hal::TouchEvent&) override { return false; }
};
class StubClock : public hal::Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t now() override { return unix_; }
  void set(int64_t u) override { unix_ = u; }
  uint32_t ms = 1000;
  int64_t unix_ = 0;
};
class StubPower : public hal::Power {
 public:
  int batteryMv() override { return 3900; }
  int batteryPercent() override { return 70; }
  bool charging() override { return false; }
  hal::Wake lightSleep(uint32_t) override { return hal::Wake::Timer; }
  void powerOff(uint32_t) override {}
};

struct Env {
  TempRoots roots;
  StubDisplay display;
  StubInput input;
  StubClock clock;
  ScriptedNet net;
  StubPower power;
  hal::Platform platform{display, input, clock, net, power, "test"};
  Config config;
  Secrets secrets;
  NetService svc{platform, config, secrets};
  explicit Env(const std::string& card) {
    fs::writeFile("/sd/paperos_config", card);
    secrets.load();
  }
};

}  // namespace

TEST_CASE("net: nothing configured means no attempt at all") {
  Env e("HA_TOKEN=x\n");
  auto r = e.svc.ensureConnected();
  CHECK_FALSE(r.ok);
  CHECK(r.error == "No Wi-Fi networks configured");
  CHECK(e.net.connects.empty());
  CHECK(e.net.scans == 0);
}

TEST_CASE("net: no network joined before -> one scan, the strongest known network, then it is remembered") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\nWIFI_SSID_2=Work\nWIFI_PASSWORD_2=w\n");
  e.net.world = {{"Work", -80, true, 1, "W1"}, {"Home", -60, true, 11, "H1"}, {"Home", -45, true, 6, "H2"}, {"Other", -30, true, 3, "O1"}};
  auto r = e.svc.ensureConnected();
  REQUIRE(r.ok);
  CHECK_FALSE(r.fast);
  CHECK(r.ssid == "Home");
  CHECK(e.net.scans == 1);
  REQUIRE(e.net.connects.size() == 1);
  CHECK(e.net.connects[0].bssid == "H2");   // the strongest access point of the strongest known network
  CHECK(e.net.connects[0].channel == 6);
  CHECK(e.svc.store().last() == "Home");
  auto m = e.svc.store().memoryOf("Home");
  REQUIRE(m.has_value());
  CHECK(m->bssid == "H2");
  CHECK(m->channel == 6);
  WifiStore reread;                          // and it was saved
  REQUIRE(reread.load());
  CHECK(reread.last() == "Home");

  // already connected: no radio calls
  e.net.connects.clear();
  e.net.scans = 0;
  CHECK(e.svc.ensureConnected().ok);
  CHECK(e.net.connects.empty());
  CHECK(e.net.scans == 0);
}

TEST_CASE("net: the fast path joins the last network with its stored access point, no scan") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\nWIFI_SSID_2=Work\nWIFI_PASSWORD_2=w\n");
  e.svc.store().remember("Work", "W1", 1);
  auto r = e.svc.ensureConnected();
  REQUIRE(r.ok);
  CHECK(r.fast);
  CHECK(r.ssid == "Work");
  CHECK(e.net.scans == 0);
  REQUIRE(e.net.connects.size() == 1);
  CHECK(e.net.connects[0].ssid == "Work");
  CHECK(e.net.connects[0].bssid == "W1");
  CHECK(e.net.connects[0].channel == 1);
}

TEST_CASE("net: a failed fast attempt is followed by exactly one scan attempt, never a loop") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\nWIFI_SSID_2=Work\nWIFI_PASSWORD_2=w\n");
  e.svc.store().remember("Work", "W1", 1);
  e.net.world = {{"Home", -50, true, 6, "H1"}, {"Work", -70, true, 1, "W1"}};
  e.net.accepts = [](const ScriptedNet::Call& c) { return c.ssid == "Home"; };  // Work's access point is gone
  auto r = e.svc.ensureConnected();
  REQUIRE(r.ok);
  CHECK_FALSE(r.fast);
  CHECK(r.ssid == "Home");                     // the strongest known network of the scan
  CHECK(e.net.scans == 1);
  CHECK(e.net.connects.size() == 2);           // the fast attempt and the scan attempt
  CHECK(e.net.connects[0].bssid == "W1");
  CHECK(e.net.connects[1].ssid == "Home");

  // both fail: reported, with the scan, and no third attempt
  Env f("WIFI_SSID=Home\nWIFI_PASSWORD=h\n");
  f.svc.store().remember("Home", "H0", 3);
  f.net.world = {{"Home", -50, true, 6, "H1"}};
  f.net.accepts = [](const ScriptedNet::Call&) { return false; };
  auto bad = f.svc.ensureConnected();
  CHECK_FALSE(bad.ok);
  CHECK(bad.error == "Could not join Home");
  CHECK(bad.scan == "Home -50 dBm ch6");
  CHECK(f.net.connects.size() == 2);
  CHECK(f.net.scans == 1);
}

TEST_CASE("net: no known network in range is reported with the scan result and nothing is joined") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\n");  // maybe only on 5 GHz: the 2.4 GHz scan will not show it
  e.net.world = {{"Neighbour", -60, true, 1, "N1"}, {"Cafe", -70, false, 6, "C1"}};
  auto r = e.svc.ensureConnected();
  CHECK_FALSE(r.ok);
  CHECK(r.error == "No known network in range");
  CHECK(r.scan == "Neighbour -60 dBm ch1, Cafe -70 dBm ch6");
  CHECK(e.net.connects.empty());
  CHECK(e.net.scans == 1);
}

TEST_CASE("net: perform connects on demand, fails with a clear message offline") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\n");
  e.net.world = {{"Home", -50, true, 6, "H1"}};
  hal::HttpRequest req;
  req.url = "https://ha.example/api/";
  auto resp = e.svc.perform(req);
  CHECK(resp.status == 200);
  CHECK(resp.body == "https://ha.example/api/");
  CHECK(e.net.requests == 1);

  Env f("WIFI_SSID=Home\nWIFI_PASSWORD=h\n");
  auto off = f.svc.perform(req);
  CHECK(off.status == 0);
  CHECK(off.error.rfind("No Wi-Fi: ", 0) == 0);
  CHECK(f.net.requests == 0);
}

TEST_CASE("net: Wi-Fi goes off after the linger time, not while a request runs; one handshake window for a refresh") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\n");
  e.net.world = {{"Home", -50, true, 6, "H1"}};
  e.config.net.lingerSec = 20;
  hal::HttpRequest req;
  req.url = "https://x/";
  e.clock.ms = 5000;
  e.svc.perform(req);
  e.svc.perform(req);  // a second request of the same refresh: no new connect
  CHECK(e.net.connects.size() == 1);
  CHECK(e.svc.msToNextDeadline() == 20000);
  e.clock.ms += 19999;
  e.svc.tick();
  CHECK(e.net.up);
  CHECK(e.net.offs == 0);
  e.clock.ms += 1;
  e.svc.tick();
  CHECK_FALSE(e.net.up);
  CHECK(e.net.offs == 1);
  CHECK(e.svc.msToNextDeadline() == kNoDeadline);
}

TEST_CASE("net: the clock is synced once after a connect, applied on the UI thread, then once a day") {
  Env e("WIFI_SSID=Home\nWIFI_PASSWORD=h\n");
  e.net.world = {{"Home", -50, true, 6, "H1"}};
  int changes = 0;
  e.svc.onChange = [&] { ++changes; };
  e.svc.ensureConnected();
  CHECK(e.clock.unix_ == 0);  // not yet: the worker only records it
  e.svc.tick();
  CHECK(e.clock.unix_ == 1800000000);
  CHECK(changes >= 1);

  // a later reconnect within a day does not sync again
  e.svc.disconnect();
  e.net.time = 1800000500;
  e.svc.ensureConnected();
  e.svc.tick();
  CHECK(e.clock.unix_ == 1800000000);
  // but a day later it does
  e.clock.unix_ = 1800000000 + 25 * 3600;
  e.svc.disconnect();
  e.net.time = 1800200000;
  e.svc.ensureConnected();
  e.svc.tick();
  CHECK(e.clock.unix_ == 1800200000);
}

TEST_CASE("net: connectTo joins a chosen network and stores it on the device") {
  Env e("HA_TOKEN=x\n");
  auto r = e.svc.connectTo("Phone", "pw", true);
  REQUIRE(r.ok);
  REQUIRE(e.svc.store().networks().size() == 1);
  CHECK(e.svc.store().networks()[0].ssid == "Phone");
  CHECK(e.svc.store().last() == "Phone");
  e.net.accepts = [](const ScriptedNet::Call&) { return false; };
  auto bad = e.svc.connectTo("Other", "x", true);
  CHECK_FALSE(bad.ok);
  CHECK(e.svc.store().networks().size() == 1);  // a failed join is not stored
}

TEST_CASE("net: the Home Assistant URL comes from the card, else the config; never a trailing slash") {
  Env e("HA_URL=https://ha.example.test/\n");
  CHECK(e.svc.haBaseUrl() == "https://ha.example.test");
  Env f("HA_TOKEN=x\n");
  f.config.ha.baseUrl = "http://homeassistant.local:8123";
  CHECK(f.svc.haBaseUrl() == "http://homeassistant.local:8123");
}

TEST_CASE("fixtureFileName: method, host and path; query gets a hash") {
  CHECK(fixtureFileName("POST", "https://ha.local/api/template") == "POST_ha.local_api_template.json");
  CHECK(fixtureFileName("GET", "http://ha.local:8123/api/states/sensor.paperos_whatsapp_inbox") ==
        "GET_ha.local_api_states_sensor.paperos_whatsapp_inbox.json");
  CHECK(fixtureFileName("GET", "https://ha.example.test/api/") == "GET_ha.example.test_api_.json");
  CHECK(fixtureFileName("GET", "https://x.y") == "GET_x.y_.json");
  auto a = fixtureFileName("GET", "https://h/p?a=1");
  auto b = fixtureFileName("GET", "https://h/p?a=2");
  CHECK(a != b);
  CHECK(a.rfind("GET_h_p_", 0) == 0);
  CHECK(a == fixtureFileName("GET", "https://h/p?a=1"));
}
