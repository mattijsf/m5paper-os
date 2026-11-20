#include "apps/pixelpuzzle/pixelpuzzle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

#include "apps/somdoku/somdoku.h"  // dates and the clock text, shared by the daily games

namespace pos::pixelpuzzle {

namespace {

// Drawn for PaperOS. Every one is solvable line by line (tests/unit/pixelpuzzle_test.cpp checks it, mirrored too).
const std::vector<Picture> kPictures = {
    {"Een hart",
     {"..........", ".##....##.", "####..####", "##########", "##########", ".########.", "..######..", "...####...",
      "....##....", ".........."}},
    {"Een huis",
     {"....##....", "...####...", "..######..", ".########.", "##########", ".#......#.", ".#.##.#.#.", ".#.##.#.#.",
      ".#.##...#.", ".########."}},
    {"Een kat",
     {"#........#", "##......##", "###....###", "##########", "#..##..#.#", "##########", "###.##.###", "##########",
      ".########.", "..######.."}},
    {"Een boom",
     {"....##....", "...####...", "..######..", ".########.", "..######..", ".########.", "##########", "....##....",
      "....##....", "..######.."}},
    {"Een zeilboot",
     {"2222#22222", "2222##2222", "2222###222", "2222####22", "2222#####2", "2222#22222", "##########", ".########.",
      "..######..", "5555555555"}},
    {"Een paddenstoel",
     {"...####...", ".########.", "##..##..##", "##########", ".########.", "...#..#...", "...#..#...", "...#..#...",
      "...####...", "5555555555"}},
    {"Een vis",
     {"..........", "....###...", "#..#####..", "##########", "#######.##", "##########", "#..#####..", "....###...",
      "..........", ".........."}},
    {"Een ster",
     {"....##....", "....##....", "...####...", "##########", ".########.", "..######..", "..######..", ".###..###.",
      ".##....##.", "##......##"}},
    {"De maan",
     {"...####...", "..###.....", ".###......", ".##.......", "###.......", "###.......", ".##.......", ".###......",
      "..###.....", "...####..."}},
    {"Een paraplu",
     {"....##....", "..######..", ".########.", "##########", "##########", "....##....", "....##....", "....##....",
      ".##.##....", "..####...."}},
    {"Een appel",
     {".....#....", "....#.....", "..##.###..", ".########.", "##########", "##########", "##########", ".########.",
      ".########.", "..##..##.."}},
    {"Een smiley",
     {"..######..", ".########.", "##.####.##", "##.####.##", "##########", "#.######.#", "##.####.##", "###....###",
      ".########.", "..######.."}},
    {"Een mijter",
     {"....##....", "...####...", "..######..", "..##..##..", "..#....#..", "..##..##..", "..######..", "..######..",
      "..#....#..", "..######.."}},
    {"Een schoen",
     {"..........", "..........", "..........", "......###.", "......####", "#.....####", "##########", "##########",
      "##########", ".........."}},
    {"Een sneeuwpop",
     {"...####...", "...####...", "..######..", "...####...", "..#.##.#..", ".########.", "##########", "##########",
      ".########.", "..######.."}},
    {"Een eend",
     {"..###.....", ".#####....", "##.###....", ".#####....", "...###....", "..#######.", ".#########", ".#########",
      "..#######.", "5555555555"}},
    {"Een kopje",
     {"..#..#..#.", ".#..#..#..", "..#..#..#.", "..........", "#######...", "#########.", "#######..#", "#########.",
      ".#####....", "##########"}},
    {"Een anker",
     {"....##....", "...#..#...", "....##....", "..######..", "....##....", "....##....", "#...##...#", "##..##..##",
      "##########", "...####..."}},
    {"Een raket",
     {"....##....", "...####...", "...####...", "...#..#...", "...####...", "...####...", "..######..", ".########.",
      ".##.##.##.", "....##...."}},
    {"Een bel",
     {"....##....", "...####...", "..######..", "..######..", "..######..", ".########.", ".########.", "##########",
      "..........", "....##...."}},
    {"Een muzieknoot",
     {"....######", "....######", "....#....#", "....#....#", "....#....#", "....#....#", "..###..###", ".####.####",
      ".####.####", "..##...##."}},
    {"Een slak",
     {"..........", "..######..", ".##....##.", ".#..##..#.", ".#.#..#.#.", ".#..#.##.#", ".##.....##", "##########",
      "#########.", "5555555555"}},
    {"Een kerstboom",
     {"....##....", "...####...", "....##....", "...####...", "..######..", "...####...", "..######..", ".########.",
      "....##....", "...####..."}},
    {"Een pijl",
     {"....#.....", "....##....", "....###...", "#########.", "##########", "##########", "#########.", "....###...",
      "....##....", "....#....."}},
};

}  // namespace

const std::vector<Picture>& pictures() { return kPictures; }

Puzzle fromPicture(const Picture& pic, bool mirror) {
  Puzzle p;
  p.name = pic.name;
  p.solution.assign(kN * kN, 0);
  p.shade.assign(kN * kN, 0);
  for (int r = 0; r < kN; ++r) {
    for (int c = 0; c < kN; ++c) {
      const char ch = pic.rows[r][mirror ? kN - 1 - c : c];
      const int i = r * kN + c;
      if (ch == '#') p.solution[static_cast<size_t>(i)] = 1;
      if (ch >= '1' && ch <= '9') p.shade[static_cast<size_t>(i)] = static_cast<uint8_t>(12 - (ch - '0'));  // 1 = gray(11) .. 9 = gray(3)
    }
  }
  std::vector<uint8_t> line(kN);
  for (int r = 0; r < kN; ++r) {
    for (int c = 0; c < kN; ++c) line[static_cast<size_t>(c)] = p.solution[static_cast<size_t>(r * kN + c)];
    p.rows.push_back(clueOf(line));
  }
  for (int c = 0; c < kN; ++c) {
    for (int r = 0; r < kN; ++r) line[static_cast<size_t>(r)] = p.solution[static_cast<size_t>(r * kN + c)];
    p.cols.push_back(clueOf(line));
  }
  p.ok = true;
  return p;
}

Clue clueOf(const std::vector<uint8_t>& line) {
  Clue out;
  int run = 0;
  for (uint8_t v : line) {
    if (v == Filled) {
      ++run;
    } else if (run) {
      out.push_back(run);
      run = 0;
    }
  }
  if (run) out.push_back(run);
  return out;
}

namespace {

// Line logic: every arrangement of the clue (as a bit pattern of kN cells) that agrees with what is known. A cell on which
// they all agree becomes known. known: -1 unknown, 0 empty, 1 filled. Returns false when no arrangement fits.
bool solveLine(const Clue& clue, std::vector<int>& known, bool& changed) {
  int must1 = (1 << kN) - 1, must0 = (1 << kN) - 1, any = 0;
  for (int bits = 0; bits < (1 << kN); ++bits) {
    std::vector<uint8_t> line(kN);
    bool fits = true;
    for (int c = 0; c < kN && fits; ++c) {
      line[static_cast<size_t>(c)] = (bits >> c) & 1;
      const int k = known[static_cast<size_t>(c)];
      if (k >= 0 && k != line[static_cast<size_t>(c)]) fits = false;
    }
    if (!fits || clueOf(line) != clue) continue;
    ++any;
    must1 &= bits;
    must0 &= ~bits;
  }
  if (!any) return false;
  for (int c = 0; c < kN; ++c) {
    int& k = known[static_cast<size_t>(c)];
    if (k >= 0) continue;
    if ((must1 >> c) & 1) {
      k = 1;
      changed = true;
    } else if ((must0 >> c) & 1) {
      k = 0;
      changed = true;
    }
  }
  return true;
}

}  // namespace

bool solvable(const std::vector<Clue>& rows, const std::vector<Clue>& cols, int* passes) {
  std::vector<int> grid(kN * kN, -1);
  int pass = 0;
  for (bool changed = true; changed && pass < 100;) {
    changed = false;
    ++pass;
    std::vector<int> line(kN);
    for (int r = 0; r < kN; ++r) {
      for (int c = 0; c < kN; ++c) line[static_cast<size_t>(c)] = grid[static_cast<size_t>(r * kN + c)];
      if (!solveLine(rows[static_cast<size_t>(r)], line, changed)) return false;
      for (int c = 0; c < kN; ++c) grid[static_cast<size_t>(r * kN + c)] = line[static_cast<size_t>(c)];
    }
    for (int c = 0; c < kN; ++c) {
      for (int r = 0; r < kN; ++r) line[static_cast<size_t>(r)] = grid[static_cast<size_t>(r * kN + c)];
      if (!solveLine(cols[static_cast<size_t>(c)], line, changed)) return false;
      for (int r = 0; r < kN; ++r) grid[static_cast<size_t>(r * kN + c)] = line[static_cast<size_t>(r)];
    }
  }
  if (passes) *passes = pass;
  return std::none_of(grid.begin(), grid.end(), [](int v) { return v < 0; });
}

bool lineDone(const Puzzle& p, const std::vector<uint8_t>& cells, int i) {
  std::vector<uint8_t> line(kN);
  for (int k = 0; k < kN; ++k) {
    const int idx = i < kN ? i * kN + k : k * kN + (i - kN);
    line[static_cast<size_t>(k)] = cells[static_cast<size_t>(idx)];
  }
  return clueOf(line) == (i < kN ? p.rows[static_cast<size_t>(i)] : p.cols[static_cast<size_t>(i - kN)]);
}

bool solved(const Puzzle& p, const std::vector<uint8_t>& cells) {
  if (!p.ok || cells.size() != static_cast<size_t>(kN * kN)) return false;
  for (int i = 0; i < 2 * kN; ++i)
    if (!lineDone(p, cells, i)) return false;
  return true;
}

int puzzleNumber(const std::string& day) {
  Date a, b;
  if (!somdoku::parseDay(day, &a) || !somdoku::parseDay(kFirstDay, &b)) return 0;
  return daysFromCivil(a) - daysFromCivil(b) + 1;
}

Puzzle dailyPuzzle(const std::string& day) {
  const int number = puzzleNumber(day);
  if (number < 1) return {};
  const int n = static_cast<int>(kPictures.size());
  // A fixed order that does not follow the list (neighbours in the list are alike): a step coprime with n.
  int step = 7;
  while (std::gcd(step, n) != 1) ++step;
  const int k = number - 1;
  const int index = (k % n) * step % n;
  const bool mirror = (k / n) % 2 == 1;
  return fromPicture(kPictures[static_cast<size_t>(index)], mirror);
}

std::string shareText(int number, const std::string& name, double seconds) {
  return "Pixelpuzzel " + std::to_string(number) + ": " + name + " in " + somdoku::clock(seconds);
}

}  // namespace pos::pixelpuzzle
