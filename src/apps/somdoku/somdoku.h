// Somdoku, the game logic: a port of the web game's src/lib/puzzle.js and src/lib/game.js (the user's own game,
// somdoku.nl). It must give the same puzzle, number, level and share text as the web version on the
// same day, so scores can be compared: tests/unit/somdoku_test.cpp checks 500 days against vectors written by the web
// code itself (tools/somdoku-vectors.mjs). Change nothing here without regenerating and passing those vectors.
//
// The rules (the puzzle booklet's "Japanse sommen"): a grid of n by n, the digits 1..k; every row and column holds each digit
// once and n - k black cells. Beside each row and above each column are the sums of the groups of digits that touch, in order.
// Platform-free: no I/O, no clock; the app (somdoku_app.cpp) brings the day and the time.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pos::somdoku {

constexpr int kBlack = 0;   // a black cell
constexpr int kEmpty = -1;  // not filled in (the web version's null)

// mulberry32, as the web version: the same numbers for the same seed on every device. JavaScript's uint32 and Math.imul
// arithmetic is plain uint32_t arithmetic here; next() divides by 2^32 exactly like the web version.
class Rng {
 public:
  explicit Rng(uint32_t seed) : a_(seed) {}
  double next();

 private:
  uint32_t a_;
};

// FNV-1a over the UTF-16 code units of `utf8` (JavaScript's charCodeAt), to seed Rng with a date.
uint32_t hash(const std::string& utf8);

using Clue = std::vector<int>;
Clue clueOf(const std::vector<int>& line);

struct Puzzle {
  int n = 5, k = 4, blacks = 1;
  std::vector<int> solution;  // n*n, row by row: kBlack or 1..k
  std::vector<Clue> rows, cols;
  int passes = 0;   // rounds of line-by-line reasoning the solver needed (how hard it is)
  int attempt = 0;  // which generated grid was the first that fitted
  bool ok = false;  // false only if no puzzle was found (never for the daily puzzles)
};

// Solves from the clues alone, line by line, the way a person does. Fills `grid` (n*n) and returns the number of passes,
// or -1 if that reasoning gets stuck (then the puzzle would need guessing).
int solve(int n, int k, const std::vector<Clue>& rows, const std::vector<Clue>& cols, std::vector<int>* grid = nullptr);

Puzzle generate(uint32_t seed, int n = 5, int k = 4, int minPasses = 3, int maxPasses = 99);

// --- the game around the puzzle (game.js) ---------------------------------------------------------------------------------

constexpr const char* kFirstDay = "2026-11-11";

struct Date {
  int y = 0, m = 0, d = 0;
};
std::string dayString(const Date& date);          // "2026-11-11"
bool parseDay(const std::string& day, Date* out);  // inverse of dayString
int puzzleNumber(const std::string& day);           // 1 for kFirstDay
std::string dayBefore(const std::string& day);
int levelOf(const std::string& day);  // 3, 4 or 5
Puzzle dailyPuzzle(const std::string& day);

// How many days in a row were solved, up to and including `day` (a day not solved yet does not break it).
int streak(const std::string& day, const std::function<bool(const std::string&)>& solvedOn);
std::string streakText(int count);  // "Reeks: 3 dagen"
std::string clock(double seconds);  // "2:47", "1:02:47"

// How a game went: the cells in the order they were first filled, which were changed later, how often.
struct Trail {
  std::vector<int> order;
  std::vector<bool> changed;
  int edits = 0;
  explicit Trail(int cells = 25) : changed(static_cast<size_t>(cells), false) {}
};
// `to` put in `cell` that held `from`. Filling an empty cell puts it on the timeline; altering a filled one is a change,
// unless it is a slip (put right at once, before any other cell was touched).
void noteMove(Trail& trail, int cell, int from, int to, bool slip = false);
std::string marks(const Trail& trail);  // 'g' kept, 'y' changed later, in the order of filling

extern const char* const kGreen;   // "🟩" U+1F7E9
extern const char* const kYellow;  // "🟨" U+1F7E8
// "Somdoku 12 · 3:42" and under it the timeline, n squares to a line: the text that is copied and shared.
std::string shareText(const std::string& name, double seconds, const Trail& trail, int n = 5);

}  // namespace pos::somdoku
