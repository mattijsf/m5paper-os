// Pixelpuzzel: a daily picture puzzle (a nonogram). Platform-free game logic: the pictures, the clues, the line solver that
// proves a picture needs no guessing, the daily choice, the check. docs/apps/pixelpuzzle.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pos::pixelpuzzle {

constexpr int kN = 10;  // 10 x 10 cells

// A cell as the player marks it.
enum Cell : uint8_t { Empty = 0, Filled = 1, Crossed = 2 };

using Clue = std::vector<int>;  // the runs of filled cells in a line, in order; empty for an empty line

// One picture: its name (Dutch, shown when solved) and kN rows. '#' = filled; '.' = empty; '1'..'9' = empty, drawn in
// gray when solved (1 = gray(11), light but still seen on the panel, .. 9 = gray(3)): only the reveal shows them, the puzzle does not.
struct Picture {
  const char* name;
  const char* rows[kN];
};

const std::vector<Picture>& pictures();

struct Puzzle {
  bool ok = false;
  std::string name;
  std::vector<uint8_t> solution;  // kN * kN, 1 = filled
  std::vector<uint8_t> shade;     // kN * kN, 0 = none, else the gray (0..15) of an empty cell when solved
  std::vector<Clue> rows, cols;
};

// The puzzle of a picture, mirrored left-right when `mirror`.
Puzzle fromPicture(const Picture& p, bool mirror);

// The runs of filled cells in `line` (values: anything non-zero that is Filled counts).
Clue clueOf(const std::vector<uint8_t>& line);

// Line-by-line logic only (no guessing). True when that fixes every cell; `passes` gets the number of rounds over all lines.
bool solvable(const std::vector<Clue>& rows, const std::vector<Clue>& cols, int* passes = nullptr);

// Solved: every row and column has its clue (any grid that fits counts; crosses are empty cells).
bool solved(const Puzzle& p, const std::vector<uint8_t>& cells);

// Whether line `i` (rows 0..kN-1, then columns kN..2kN-1) already has its clue: the clue is drawn gray then.
bool lineDone(const Puzzle& p, const std::vector<uint8_t>& cells, int i);

// The daily puzzle: day 1 is kFirstDay; the pictures in a fixed shuffled order, then again mirrored, and so on.
constexpr const char* kFirstDay = "2026-11-20";
int puzzleNumber(const std::string& day);
Puzzle dailyPuzzle(const std::string& day);

// "Pixelpuzzel 3: Een kat in 4:12" for the clipboard.
std::string shareText(int number, const std::string& name, double seconds);

}  // namespace pos::pixelpuzzle
