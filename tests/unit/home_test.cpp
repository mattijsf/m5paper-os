#include <doctest.h>

#include <cstdlib>
#include <set>
#include <string>
#include <unistd.h>

#include "apps/home/home_model.h"
#include "core/fs.h"

using namespace pos::home;

namespace {
struct Card {
  std::string dir;
  Card() {
    char tmpl[] = "/tmp/pos-hm-XXXXXX";
    dir = mkdtemp(tmpl);
    pos::fs::setRoots(dir + "/sd", dir + "/data");
  }
  ~Card() {
    const std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};
}  // namespace

TEST_CASE("home: the default home.json parses, with the full Evening scene and the dashboard's entities") {
  Config c;
  std::string err;
  REQUIRE(parseConfig(defaultConfigJson(), c, err));
  REQUIRE(c.tabs.size() == 3);
  CHECK(c.tabs[0].title == "Lights");
  CHECK(c.tabs[1].title == "Climate");
  CHECK(c.tabs[2].title == "More");
  const Item& evening = c.tabs[0].items[0];
  CHECK(evening.type == "scene");
  CHECK(evening.name == "Evening");
  REQUIRE(evening.calls.size() == 7);
  CHECK(evening.calls[0].service == "light.turn_on");
  CHECK(evening.calls[0].entity == "light.sofa_lamp");
  CHECK(callBody(evening.calls[0]) == "{\"brightness\":34,\"color_temp_kelvin\":2000,\"entity_id\":\"light.sofa_lamp\"}");
  CHECK(callBody(evening.calls[4]).find("\"xy_color\":[0.371,0.239]") != std::string::npos);
  CHECK(c.tabs[0].items.size() == 10);  // the scene + nine lights
  const Item& zit = c.tabs[0].items[2];
  CHECK(zit.name == "Lounge");
  CHECK(zit.entity == "light.lounge_relative");
  CHECK(zit.group == "light.lounge");
  CHECK(zit.members.size() == 3);
  CHECK(iconByName(zit.icon, pos::gen::Icon::Info) == pos::gen::Icon::Sofa);
  CHECK(iconByName("no-such-icon", pos::gen::Icon::Info) == pos::gen::Icon::Info);
}

TEST_CASE("home: config errors and odd items") {
  Config c;
  std::string err;
  CHECK_FALSE(parseConfig("not json", c, err));
  CHECK(err.find("valid JSON") != std::string::npos);
  CHECK_FALSE(parseConfig("{}", c, err));
  CHECK_FALSE(parseConfig("{\"tabs\":[]}", c, err));
  // unknown types and incomplete items are skipped; the rest stays
  REQUIRE(parseConfig(R"({"tabs":[{"title":"A","items":[
      {"type":"hologram","entity":"x.y"}, {"type":"light"}, {"type":"scene","name":"s"}, {"type":"room","name":"r"},
      {"type":"light","entity":"light.ok"},{"type":"sensor","entity":"sensor.t","name":"T"}]}]})", c, err));
  REQUIRE(c.tabs[0].items.size() == 2);
  CHECK(c.tabs[0].items[0].name == "light.ok");  // no name: the entity id
  CHECK(c.tabs[0].items[1].name == "T");
  CHECK(c.tabs[0].items[0].dimmable);
  REQUIRE(parseConfig(R"({"tabs":[{"items":[{"type":"light","entity":"light.a","dimmable":false}]}]})", c, err));
  CHECK_FALSE(c.tabs[0].items[0].dimmable);
  CHECK(c.tabs[0].title == "Tab 1");
}

TEST_CASE("home: entities to read per item and per tab, no duplicates, the scene reads nothing") {
  Config c;
  std::string err;
  REQUIRE(parseConfig(defaultConfigJson(), c, err));
  auto scene = entitiesOf(c.tabs[0].items[0]);
  CHECK(scene.empty());
  auto zit = entitiesOf(c.tabs[0].items[2]);
  REQUIRE(zit.size() == 5);  // stand-in, group, three lamps
  CHECK(zit[0] == "light.lounge_relative");
  CHECK(zit[1] == "light.lounge");
  auto tab = entitiesOf(c.tabs[0]);
  std::set<std::string> uniq(tab.begin(), tab.end());
  CHECK(uniq.size() == tab.size());
  auto room = entitiesOf(c.tabs[1].items[2]);
  CHECK(room.size() == 2);
}

TEST_CASE("home: state responses") {
  EntityState s;
  REQUIRE(parseState(R"({"entity_id":"light.x","state":"on","attributes":{"brightness":153,"friendly_name":"X"},"last_changed":"2026-11-15T10:00:00"})", s));
  CHECK(s.known);
  CHECK(s.on());
  CHECK(s.brightness == 153);
  REQUIRE(parseState(R"({"state":"off","attributes":{}})", s));
  CHECK_FALSE(s.on());
  CHECK(s.brightness == -1);
  REQUIRE(parseState(R"({"state":"unavailable","attributes":{}})", s));
  CHECK(s.unavailable());
  REQUIRE(parseState(R"({"state":"heat","attributes":{"current_temperature":20.5,"temperature":21,"unit_of_measurement":"°C"}})", s));
  CHECK(s.current == doctest::Approx(20.5));
  CHECK(s.target == doctest::Approx(21.0));
  CHECK(s.unit == "°C");
  CHECK_FALSE(parseState("", s));
  CHECK_FALSE(parseState("{\"message\":\"Entity not found.\"}", s));
  CHECK_FALSE(s.known);
  CHECK_FALSE(parseState("[1,2]", s));
}

TEST_CASE("home: light view, with and without a group") {
  Item z;
  z.type = "light";
  z.entity = "light.lounge_relative";
  z.group = "light.lounge";
  z.members = {"light.a", "light.b", "light.c"};
  States st;
  auto set = [&](const std::string& id, const std::string& state, int b = -1) {
    EntityState e;
    e.known = true;
    e.state = state;
    e.brightness = b;
    st[id] = e;
  };
  set("light.a", "on", 153);
  set("light.b", "on", 153);
  set("light.c", "off");
  set("light.lounge", "on", 153);  // the read-only group reports; the stand-in does not
  LightView v = lightView(z, st);
  CHECK(v.on);
  CHECK(v.percent == 60);
  CHECK(v.onCount == 2);
  CHECK(v.groupSize == 3);
  set("light.lounge_relative", "off");  // the stand-in reports itself: it wins
  CHECK_FALSE(lightView(z, st).on);

  Item single;
  single.type = "light";
  single.entity = "light.porch";
  set("light.porch", "unavailable");
  LightView u = lightView(single, st);
  CHECK(u.unavailable);
  CHECK(u.onCount == -1);
  CHECK(lightView(single, States{}).percent == -1);  // nothing read yet
}

TEST_CASE("home: the states cache round trip, calls and service names") {
  States st;
  EntityState a;
  a.known = true;
  a.state = "heat";
  a.current = 20.5;
  a.target = 21;
  a.unit = "°C";
  st["climate.living_room"] = a;
  EntityState b;
  b.known = true;
  b.state = "on";
  b.brightness = 200;
  st["light.x"] = b;
  st["light.never_read"] = EntityState{};  // not known: not saved
  States back = statesFromJson(statesToJson(st));
  REQUIRE(back.size() == 2);
  CHECK(back["climate.living_room"].current == doctest::Approx(20.5));
  CHECK(back["climate.living_room"].unit == "°C");
  CHECK(back["light.x"].brightness == 200);
  CHECK(back["light.x"].current < -999);
  CHECK(statesFromJson("garbage").empty());
  std::string d, n;
  REQUIRE(splitService("light.turn_on", d, n));
  CHECK(d == "light");
  CHECK(n == "turn_on");
  CHECK_FALSE(splitService("noservice", d, n));
  CHECK_FALSE(splitService(".x", d, n));
  CHECK_FALSE(splitService("x.", d, n));
  Call c{"light.turn_on", "light.x", "not json"};
  CHECK(callBody(c) == "{\"entity_id\":\"light.x\"}");
}

TEST_CASE("home: loadConfig writes the default once, the card's copy wins, a broken file falls back") {
  Card card;
  std::string note;
  Config c = loadConfig(note);
  CHECK(c.tabs.size() == 3);
  CHECK(pos::fs::exists("/data/home.json"));
  pos::fs::writeFile("/data/home.json", R"({"tabs":[{"title":"Mine","items":[{"type":"light","entity":"light.q"}]}]})");
  c = loadConfig(note);
  REQUIRE(c.tabs.size() == 1);
  CHECK(c.tabs[0].title == "Mine");
  pos::fs::writeFile("/sd/paperos/home.json", R"({"tabs":[{"title":"Card","items":[{"type":"light","entity":"light.q"}]}]})");
  CHECK(loadConfig(note).tabs[0].title == "Card");
  pos::fs::writeFile("/sd/paperos/home.json", "{ broken");
  c = loadConfig(note);
  CHECK(c.tabs.size() == 3);  // the default
  CHECK(note.find("home.json") != std::string::npos);
}
