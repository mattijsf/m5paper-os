#include <doctest.h>

#include <cstring>
#include <set>
#include <string>

#include "apps/pixelpuzzle/pixelpuzzle.h"
#include "core/timefmt.h"

using namespace pos::pixelpuzzle;

TEST_CASE("pixelpuzzle: clues are the runs of filled cells") {
  CHECK(clueOf({0, 1, 1, 0, 1, 0, 0, 1, 1, 1}) == Clue{2, 1, 3});
  CHECK(clueOf({0, 0, 0, 0, 0, 0, 0, 0, 0, 0}).empty());
  CHECK(clueOf({1, 1, 1, 1, 1, 1, 1, 1, 1, 1}) == Clue{10});
  CHECK(clueOf({1, 2, 1, 2, 2, 2, 2, 2, 2, 1}) == Clue{1, 1, 1});  // crosses are empty
}

TEST_CASE("pixelpuzzle: every picture is 10 x 10 and needs no guessing, mirrored too") {
  std::set<std::string> names;
  for (const Picture& pic : pictures()) {
    CAPTURE(pic.name);
    CHECK(names.insert(pic.name).second);
    for (const char* row : pic.rows) {
      CAPTURE(row);
      REQUIRE(std::strlen(row) == static_cast<size_t>(kN));
      for (const char* ch = row; *ch; ++ch) CHECK((*ch == '#' || *ch == '.' || (*ch >= '1' && *ch <= '9')));
    }
    for (bool mirror : {false, true}) {
      const Puzzle p = fromPicture(pic, mirror);
      int passes = 0;
      CHECK(solvable(p.rows, p.cols, &passes));
      CHECK(solved(p, p.solution));
    }
  }
  CHECK(pictures().size() >= 20);
}

TEST_CASE("pixelpuzzle: the daily puzzle, its number, the check") {
  CHECK(puzzleNumber("2026-11-20") == 1);
  CHECK(puzzleNumber("2026-11-21") == 2);
  CHECK(puzzleNumber("2026-11-19") == 0);
  CHECK_FALSE(dailyPuzzle("2026-11-19").ok);
  // Every picture once before one comes back; then all of them mirrored.
  const int n = static_cast<int>(pictures().size());
  std::set<std::string> seen;
  for (int d = 0; d < n; ++d) {
    const Puzzle p = dailyPuzzle(pos::isoDate(pos::civilFromDays(pos::daysFromCivil(pos::Date{2026, 11, 20}) + d)));
    REQUIRE(p.ok);
    seen.insert(p.name);
  }
  CHECK(static_cast<int>(seen.size()) == n);

  const Puzzle p = dailyPuzzle("2026-11-20");
  std::vector<uint8_t> cells(kN * kN, Empty);
  CHECK_FALSE(solved(p, cells));
  for (size_t i = 0; i < cells.size(); ++i) cells[i] = p.solution[i] ? Filled : Crossed;
  CHECK(solved(p, cells));
  CHECK(lineDone(p, cells, 0));
  CHECK(shareText(1, "Een kat", 252) == "Pixelpuzzel 1: Een kat in 4:12");
}
