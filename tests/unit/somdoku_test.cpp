// Somdoku port: the device must give the same puzzle, number, level and share text as the web game (somdoku.nl). The vectors
// in tests/unit/data/somdoku_vectors.json were written by the web game's own code (tools/somdoku-vectors.mjs, 500 days).
#include <doctest.h>

#include <ArduinoJson.h>

#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "apps/somdoku/somdoku.h"

using namespace pos::somdoku;

namespace {

JsonDocument& vectors() {
  static JsonDocument doc;
  static bool loaded = false;
  if (!loaded) {
    std::ifstream f("tests/unit/data/somdoku_vectors.json");
    std::stringstream ss;
    ss << f.rdbuf();
    REQUIRE_MESSAGE(!deserializeJson(doc, ss.str()), "tests/unit/data/somdoku_vectors.json missing or invalid");
    loaded = true;
  }
  return doc;
}

std::vector<int> ints(JsonArrayConst a) {
  std::vector<int> out;
  for (JsonVariantConst v : a) out.push_back(v.as<int>());
  return out;
}

int value(JsonVariantConst v) { return v.isNull() ? kEmpty : v.as<int>(); }

}  // namespace

TEST_CASE("somdoku: hash and rng match JavaScript") {
  JsonDocument& d = vectors();
  for (JsonObjectConst h : d["hashes"].as<JsonArrayConst>())
    CHECK_MESSAGE(hash(h["text"].as<std::string>()) == h["hash"].as<uint32_t>(), h["text"].as<std::string>());
  Rng r(hash("sommetje 2026-11-11"));
  // JSON doubles may lose the last bit when parsed: compare the uint32 each one stands for.
  for (JsonVariantConst x : d["randoms"].as<JsonArrayConst>())
    CHECK(std::llround(r.next() * 4294967296.0) == std::llround(x.as<double>() * 4294967296.0));
}

TEST_CASE("somdoku: 500 daily puzzles are the web game's, cell for cell") {
  JsonDocument& d = vectors();
  CHECK(d["firstDay"].as<std::string>() == kFirstDay);
  int days = 0;
  for (JsonObjectConst v : d["days"].as<JsonArrayConst>()) {
    const std::string day = v["day"].as<std::string>();
    CAPTURE(day);
    CHECK(puzzleNumber(day) == v["number"].as<int>());
    CHECK(levelOf(day) == v["level"].as<int>());
    const Puzzle p = dailyPuzzle(day);
    REQUIRE(p.ok);
    CHECK(p.passes == v["passes"].as<int>());
    CHECK(p.attempt == v["attempt"].as<int>());
    CHECK(p.solution == ints(v["solution"]));
    std::vector<Clue> rows, cols;
    for (JsonArrayConst c : v["rows"].as<JsonArrayConst>()) rows.push_back(ints(c));
    for (JsonArrayConst c : v["cols"].as<JsonArrayConst>()) cols.push_back(ints(c));
    CHECK(p.rows == rows);
    CHECK(p.cols == cols);
    ++days;
  }
  CHECK(days == 500);
}

TEST_CASE("somdoku: every daily puzzle follows the rules and solves from its clues alone") {
  for (std::string day = "2027-03-20"; day != "2027-02-18"; day = dayBefore(day)) {
    const Puzzle p = dailyPuzzle(day);
    REQUIRE(p.ok);
    for (int r = 0; r < 5; ++r) {
      std::set<int> row, col;
      for (int c = 0; c < 5; ++c) {
        row.insert(p.solution[static_cast<size_t>(r * 5 + c)]);
        col.insert(p.solution[static_cast<size_t>(c * 5 + r)]);
      }
      CHECK(row == std::set<int>{0, 1, 2, 3, 4});
      CHECK(col == std::set<int>{0, 1, 2, 3, 4});
    }
    std::vector<int> grid;
    CHECK(solve(5, 4, p.rows, p.cols, &grid) == p.passes);
    CHECK(grid == p.solution);
  }
}

TEST_CASE("somdoku: share texts, clock and streak text are the web game's, byte for byte") {
  JsonDocument& d = vectors();
  for (JsonObjectConst s : d["shares"].as<JsonArrayConst>()) {
    Trail trail(25);
    for (JsonArrayConst m : s["moves"].as<JsonArrayConst>())
      noteMove(trail, m[0].as<int>(), value(m[1]), value(m[2]), m[3].as<bool>());
    CHECK(marks(trail) == s["marks"].as<std::string>());
    CHECK(shareText(s["name"].as<std::string>(), s["seconds"].as<double>(), trail) == s["text"].as<std::string>());
  }
  for (JsonObjectConst c : d["clocks"].as<JsonArrayConst>()) CHECK(clock(c["seconds"].as<double>()) == c["text"].as<std::string>());
  for (JsonObjectConst c : d["streakTexts"].as<JsonArrayConst>()) CHECK(streakText(c["n"].as<int>()) == c["text"].as<std::string>());
}

TEST_CASE("somdoku: days, numbers and streaks") {
  CHECK(puzzleNumber("2026-11-11") == 1);
  CHECK(puzzleNumber("2026-11-16") == 6);
  CHECK(puzzleNumber("2027-01-01") == 52);
  CHECK(dayBefore("2027-01-01") == "2026-12-31");
  CHECK(dayBefore("2028-03-01") == "2028-02-29");
  Date d;
  CHECK(parseDay("2026-11-16", &d));
  CHECK(dayString(d) == "2026-11-16");
  CHECK_FALSE(parseDay("nonsense", &d));
  const std::set<std::string> solved = {"2026-11-13", "2026-11-14", "2026-11-15"};
  const auto on = [&](const std::string& day) { return solved.count(day) > 0; };
  CHECK(streak("2026-11-15", on) == 3);
  CHECK(streak("2026-11-16", on) == 3);  // today not solved yet: it does not break the streak
  CHECK(streak("2026-11-17", on) == 0);
}
