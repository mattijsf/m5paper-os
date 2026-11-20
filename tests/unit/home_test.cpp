#include <doctest.h>

#include <algorithm>

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
  REQUIRE(c.tabs.size() == 4);
  CHECK(c.tabs[0].title == "Lights");
  CHECK(c.tabs[1].title == "Climate");
  CHECK(c.tabs[2].title == "More");
  CHECK(c.tabs[3].title == "Music");
  REQUIRE(c.tabs[3].items.size() == 1);
  CHECK(c.tabs[3].items[0].type == "media");
  CHECK(c.tabs[3].items[0].entity == "media_player.living_room");
  const Item& evening = c.tabs[0].items[0];
  CHECK(evening.type == "scene");
  CHECK(evening.name == "Evening");
  REQUIRE(evening.calls.size() == 7);
  CHECK(evening.calls[0].service == "light.turn_on");
  CHECK(evening.calls[0].entity == "light.sofa_lamp");
  CHECK(callBody(evening.calls[0]) == "{\"brightness\":34,\"color_temp_kelvin\":2000,\"entity_id\":\"light.sofa_lamp\"}");
  CHECK(callBody(evening.calls[4]).find("\"xy_color\":[0.371,0.239]") != std::string::npos);
  // As the house dashboard's Thuis tab (2026-11-16): two scenes, the Sofa lamp first, the living room, the kitchen, "All
  // downstairs", then outside and upstairs.
  CHECK(c.tabs[0].items.size() == 12);
  CHECK(c.tabs[0].items[1].name == "Dining default");
  CHECK(c.tabs[0].items[1].calls.size() == 2);
  const Item& hoek = c.tabs[0].items[2];
  CHECK(hoek.name == "Sofa lamp");
  CHECK(hoek.entity == "light.sofa_lamp");
  CHECK(iconByName(hoek.icon, pos::gen::Icon::Info) == pos::gen::Icon::LampFloor);
  const Item& all = c.tabs[0].items[8];
  CHECK(all.name == "All downstairs");
  REQUIRE(all.also.size() == 1);
  CHECK(all.also[0] == "light.kitchen_spots_relative");
  CHECK_FALSE(all.dimmable);
  for (const Item& it : c.tabs[0].items) CHECK(it.entity != "light.hall");  // gone since version 2
  CHECK(configVersion(defaultConfigJson()) == 3);
  CHECK(nameOf(c, "light.coffee_table") == "Coffee table");  // the dashboard's lamp names
  CHECK(nameOf(c, "light.some_other_lamp") == "some other lamp");
  CHECK(configVersion("{\"tabs\":[]}") == 1);
  const Item& zit = c.tabs[0].items[4];
  CHECK(zit.name == "Lounge");
  CHECK(zit.entity == "light.lounge_relative");
  CHECK(zit.group == "light.lounge");
  CHECK(zit.members.size() == 3);
  CHECK(iconByName(zit.icon, pos::gen::Icon::Info) == pos::gen::Icon::Sofa);
  CHECK(iconByName("no-such-icon", pos::gen::Icon::Info) == pos::gen::Icon::Info);
}

TEST_CASE("home: \"All downstairs\" is on while any of its lamps is, and counts them") {
  Config c;
  std::string err;
  REQUIRE(parseConfig(defaultConfigJson(), c, err));
  const Item& all = c.tabs[0].items[8];
  States st;
  st["light.downstairs_relative"].known = true;
  st["light.downstairs_relative"].state = "off";
  st["light.kitchen_spots"].known = true;
  st["light.kitchen_spots"].state = "on";  // only the kitchen
  LightView v = lightView(all, st);
  CHECK(v.on);
  CHECK(v.onCount == 1);
  CHECK(v.groupSize == 7);
  st["light.kitchen_spots"].state = "off";
  CHECK_FALSE(lightView(all, st).on);
  const auto ids = entitiesOf(all);
  CHECK(std::find(ids.begin(), ids.end(), "light.kitchen_spots_relative") != ids.end());
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
  auto zit = entitiesOf(c.tabs[0].items[4]);
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
  CHECK(c.tabs.size() == 4);
  CHECK(pos::fs::exists("/data/home.json"));
  // An internal copy as new as the default stays; an older one (no "version": the list the firmware wrote before) is replaced.
  pos::fs::writeFile("/data/home.json", R"({"version":3,"tabs":[{"title":"Mine","items":[{"type":"light","entity":"light.q"}]}]})");
  c = loadConfig(note);
  REQUIRE(c.tabs.size() == 1);
  CHECK(c.tabs[0].title == "Mine");
  pos::fs::writeFile("/data/home.json", R"({"tabs":[{"title":"Old","items":[{"type":"light","entity":"light.hall"}]}]})");
  c = loadConfig(note);
  CHECK(c.tabs.size() == 4);
  std::string saved;
  REQUIRE(pos::fs::readFile("/data/home.json", saved));
  CHECK(configVersion(saved) == 3);
  pos::fs::writeFile("/sd/paperos/home.json", R"({"tabs":[{"title":"Card","items":[{"type":"light","entity":"light.q"}]}]})");
  CHECK(loadConfig(note).tabs[0].title == "Card");
  pos::fs::writeFile("/sd/paperos/home.json", "{ broken");
  c = loadConfig(note);
  CHECK(c.tabs.size() == 4);  // the default
  CHECK(note.find("home.json") != std::string::npos);
}

TEST_CASE("home: a media player's attributes, its view and its saved copy") {
  EntityState s;
  REQUIRE(parseState(R"({"entity_id":"media_player.living_room","state":"playing","attributes":{"volume_level":0.24,"is_volume_muted":false,
    "media_title":"Voices In My Head","media_artist":"Sonata Arctica","media_album_name":"Reckoning Night","media_playlist":"Metalmix",
    "source":"Spotify Connect","supported_features":8321599}})", s));
  CHECK(s.volume == 24);
  CHECK_FALSE(s.muted);
  CHECK(s.features == 8321599);
  Item it;
  it.type = "media";
  it.entity = "media_player.living_room";
  States st{{it.entity, s}};
  MediaView v = mediaView(it, st);
  CHECK(v.playing);
  CHECK(v.stateText == "Playing");
  CHECK(v.title == "Voices In My Head");
  CHECK(v.detail == "Sonata Arctica \xC2\xB7 Reckoning Night");
  CHECK(v.origin == "Spotify \xC2\xB7 Metalmix");  // "Connect" left out
  CHECK(v.volume == 24);
  CHECK(v.canPrevious);
  CHECK(v.canNext);
  CHECK(v.canVolume);
  CHECK(v.canMute);

  // The saved copy keeps everything the card shows.
  States back = statesFromJson(statesToJson(st));
  MediaView b = mediaView(it, back);
  CHECK(b.title == v.title);
  CHECK(b.detail == v.detail);
  CHECK(b.origin == v.origin);
  CHECK(b.volume == 24);
  CHECK(back[it.entity].features == 8321599);

  // Paused and muted; a player without previous / next / mute.
  REQUIRE(parseState(R"({"state":"paused","attributes":{"volume_level":0.5,"is_volume_muted":true,"media_title":"X",
    "media_album_artist":"Band","supported_features":4}})", s));
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK(v.paused);
  CHECK(v.stateText == "Paused");
  CHECK(v.detail == "Band");
  CHECK(v.muted);
  CHECK(statesFromJson(statesToJson(st))[it.entity].muted);
  CHECK(v.canVolume);
  CHECK_FALSE(v.canPrevious);
  CHECK_FALSE(v.canNext);
  CHECK_FALSE(v.canMute);

  // The TV input: no title, the source is what plays.
  REQUIRE(parseState(R"({"state":"playing","attributes":{"source":"TV"}})", s));
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK(v.title == "TV");
  CHECK(v.origin.empty());
  CHECK(v.volume == -1);
  CHECK(v.canNext);  // no supported_features: everything shows
  REQUIRE(parseState(R"({"state":"playing","attributes":{"media_title":"TV","source":"TV"}})", s));  // as the real Sonos reports it
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK(v.title == "TV");
  CHECK(v.origin.empty());

  // Idle, unavailable, not read.
  REQUIRE(parseState(R"({"state":"idle","attributes":{"media_title":"Old song"}})", s));
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK(v.title == "Nothing playing");
  CHECK(v.stateText.empty());
  CHECK(v.detail.empty());
  REQUIRE(parseState(R"({"state":"unavailable","attributes":{}})", s));
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK(v.unavailable);
  CHECK(v.stateText == "Unavailable");
  CHECK_FALSE(mediaView(it, States{}).known);
}

TEST_CASE("home: a media player's shuffle, repeat, stations and cover") {
  EntityState s;
  REQUIRE(parseState(R"({"state":"playing","attributes":{"media_title":"Kings & Queens","media_artist":"Ava Max","source":"Spotify Connect",
    "shuffle":false,"repeat":"all","source_list":["TV","KINK","NPO Radio 2"],"supported_features":8321599,
    "entity_picture":"/api/media_player_proxy/media_player.living_room?token=abc&cache=1"}})", s));
  Item it;
  it.type = "media";
  it.entity = "media_player.living_room";
  States st{{it.entity, s}};
  MediaView v = mediaView(it, st);
  CHECK(v.canShuffle);
  CHECK_FALSE(v.shuffle);
  CHECK(v.canRepeat);
  CHECK(v.repeat == "all");
  CHECK(v.canSource);
  REQUIRE(v.sources.size() == 3);
  CHECK(v.sources[1] == "KINK");
  CHECK(v.sourceText == "Spotify");
  CHECK(v.source == "Spotify Connect");
  CHECK_FALSE(v.coverKey.empty());
  CHECK(v.picture.rfind("/api/media_player_proxy/", 0) == 0);

  // A new token in the address alone keeps the cover; another song changes it.
  const std::string key = v.coverKey;
  st[it.entity].picture = "/api/media_player_proxy/media_player.living_room?token=def&cache=1";
  CHECK(mediaView(it, st).coverKey == key);
  st[it.entity].title = "Sweet but Psycho";
  CHECK(mediaView(it, st).coverKey != key);

  // The saved copy keeps shuffle, repeat and the stations, never the address with its token.
  const std::string json = statesToJson(st);
  CHECK(json.find("token") == std::string::npos);
  States back = statesFromJson(json);
  MediaView b = mediaView(it, back);
  CHECK(b.canShuffle);
  CHECK(b.repeat == "all");
  CHECK(b.sources.size() == 3);
  CHECK(b.coverKey.empty());

  // Without the features (or the state) the controls stay away.
  REQUIRE(parseState(R"({"state":"playing","attributes":{"media_title":"X","shuffle":true,"repeat":"off","source_list":["TV"],
    "supported_features":52}})", s));
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK_FALSE(v.canShuffle);
  CHECK_FALSE(v.canRepeat);
  CHECK_FALSE(v.canSource);
  REQUIRE(parseState(R"({"state":"playing","attributes":{"media_title":"X"}})", s));
  st[it.entity] = s;
  v = mediaView(it, st);
  CHECK_FALSE(v.canShuffle);
  CHECK_FALSE(v.canRepeat);
  CHECK_FALSE(v.canSource);
  CHECK(v.coverKey.empty());

  CHECK(nextRepeat("off") == "all");
  CHECK(nextRepeat("all") == "one");
  CHECK(nextRepeat("one") == "off");
}
