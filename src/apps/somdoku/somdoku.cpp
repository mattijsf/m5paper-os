#include "apps/somdoku/somdoku.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace pos::somdoku {

const char* const kGreen = "\xF0\x9F\x9F\xA9";   // U+1F7E9 large green square
const char* const kYellow = "\xF0\x9F\x9F\xA8";  // U+1F7E8 large yellow square

double Rng::next() {
  a_ = a_ + 0x6d2b79f5u;
  uint32_t t = a_;
  t = (t ^ (t >> 15)) * (t | 1u);
  t ^= t + (t ^ (t >> 7)) * (t | 61u);
  return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

namespace {

// UTF-8 to UTF-16 code units, as JavaScript strings hold text. Invalid bytes count as U+FFFD.
std::vector<uint16_t> utf16(const std::string& s) {
  std::vector<uint16_t> out;
  for (size_t i = 0; i < s.size();) {
    const auto b = static_cast<unsigned char>(s[i]);
    uint32_t cp = 0xFFFD;
    size_t len = 1;
    if (b < 0x80) {
      cp = b;
    } else if ((b >> 5) == 0x6 && i + 1 < s.size()) {
      cp = ((b & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
      len = 2;
    } else if ((b >> 4) == 0xE && i + 2 < s.size()) {
      cp = ((b & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
           (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
      len = 3;
    } else if ((b >> 3) == 0x1E && i + 3 < s.size()) {
      cp = ((b & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
           ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
      len = 4;
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<uint16_t>(cp));
    }
    i += len;
  }
  return out;
}

std::string key(const Clue& clue) {
  std::string k;
  for (size_t i = 0; i < clue.size(); ++i) {
    if (i) k += ',';
    k += std::to_string(clue[i]);
  }
  return k;
}

using Lines = std::vector<std::vector<int>>;

// Every way to fill a line of n cells with each of the digits 1..k once and black cells, by clue (cached per n/k).
const std::map<std::string, Lines>& linesByClue(int n, int k) {
  static std::map<std::string, std::map<std::string, Lines>> cache;
  const std::string id = std::to_string(n) + "/" + std::to_string(k);
  auto found = cache.find(id);
  if (found != cache.end()) return found->second;
  std::map<std::string, Lines>& byClue = cache[id];
  std::vector<int> line(static_cast<size_t>(n), kBlack);
  const int every = ((1 << (k + 1)) - 1) & ~1;
  std::function<void(int, int)> walk = [&](int i, int used) {
    if (i == n) {
      if (used != every) return;
      byClue[key(clueOf(line))].push_back(line);
      return;
    }
    line[static_cast<size_t>(i)] = kBlack;
    walk(i + 1, used);
    for (int d = 1; d <= k; ++d) {
      if (used & (1 << d)) continue;
      line[static_cast<size_t>(i)] = d;
      walk(i + 1, used | (1 << d));
    }
    line[static_cast<size_t>(i)] = kBlack;
  };
  walk(0, 0);
  return byClue;
}

int highestBit(int bits) {
  int b = -1;
  while (bits) {
    bits >>= 1;
    ++b;
  }
  return b;
}

// A filled grid: a Latin square of n symbols whose highest ones become the black cells (randomGrid in puzzle.js; the
// shuffle and the order of random() calls are the web version's, so the same seed gives the same grid).
std::vector<int> randomGrid(Rng& random, int n, int k) {
  std::vector<int> grid(static_cast<size_t>(n * n), 0);
  std::vector<int> inRow(static_cast<size_t>(n), 0), inCol(static_cast<size_t>(n), 0);
  std::function<bool(int)> fill = [&](int i) -> bool {
    if (i == n * n) return true;
    const int r = i / n;
    const int c = i % n;
    std::vector<int> symbols(static_cast<size_t>(n));
    for (int s = 0; s < n; ++s) symbols[static_cast<size_t>(s)] = s + 1;
    for (int a = n - 1; a > 0; --a) {
      const int b = static_cast<int>(std::floor(random.next() * (a + 1)));
      std::swap(symbols[static_cast<size_t>(a)], symbols[static_cast<size_t>(b)]);
    }
    for (int symbol : symbols) {
      if ((inRow[static_cast<size_t>(r)] | inCol[static_cast<size_t>(c)]) & (1 << symbol)) continue;
      grid[static_cast<size_t>(i)] = symbol;
      inRow[static_cast<size_t>(r)] |= 1 << symbol;
      inCol[static_cast<size_t>(c)] |= 1 << symbol;
      if (fill(i + 1)) return true;
      inRow[static_cast<size_t>(r)] &= ~(1 << symbol);
      inCol[static_cast<size_t>(c)] &= ~(1 << symbol);
    }
    return false;
  };
  fill(0);
  for (int& v : grid) v = v > k ? kBlack : v;
  return grid;
}

// Days since 1970-01-01 for a proleptic Gregorian date (Howard Hinnant's days_from_civil).
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

Date civilFromDays(long z) {
  z += 719468;
  const long era = (z >= 0 ? z : z - 146096) / 146097;
  const long doe = z - era * 146097;
  const long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const long mp = (5 * doy + 2) / 153;
  Date out;
  out.d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  out.m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  out.y = static_cast<int>(yoe + era * 400 + (out.m <= 2));
  return out;
}

const int kLevels[] = {3, 4, 5};

}  // namespace

uint32_t hash(const std::string& utf8) {
  uint32_t h = 0x811c9dc5u;
  for (uint16_t unit : utf16(utf8)) {
    h ^= unit;
    h *= 0x01000193u;
  }
  return h;
}

Clue clueOf(const std::vector<int>& line) {
  Clue sums;
  int run = 0;
  bool open = false;
  for (int value : line) {
    if (value > 0) {
      run += value;
      open = true;
    } else if (open) {
      sums.push_back(run);
      run = 0;
      open = false;
    }
  }
  if (open) sums.push_back(run);
  return sums;
}

int solve(int n, int k, const std::vector<Clue>& rows, const std::vector<Clue>& cols, std::vector<int>* grid) {
  const auto& all = linesByClue(n, k);
  struct Line {
    std::vector<int> cells;
    Lines fits;
  };
  std::vector<Line> lines;
  const auto fitsOf = [&](const Clue& clue) {
    auto it = all.find(key(clue));
    return it == all.end() ? Lines{} : it->second;
  };
  for (int r = 0; r < n; ++r) {
    Line line;
    for (int c = 0; c < n; ++c) line.cells.push_back(r * n + c);
    line.fits = fitsOf(rows[static_cast<size_t>(r)]);
    lines.push_back(std::move(line));
  }
  for (int c = 0; c < n; ++c) {
    Line line;
    for (int r = 0; r < n; ++r) line.cells.push_back(r * n + c);
    line.fits = fitsOf(cols[static_cast<size_t>(c)]);
    lines.push_back(std::move(line));
  }
  std::vector<int> domain(static_cast<size_t>(n * n), (1 << (k + 1)) - 1);
  int passes = 0;
  for (bool changed = true; changed;) {
    changed = false;
    ++passes;
    for (Line& line : lines) {
      Lines kept;
      for (const auto& fit : line.fits) {
        bool every = true;
        for (size_t i = 0; i < fit.size() && every; ++i) every = (domain[static_cast<size_t>(line.cells[i])] & (1 << fit[i])) != 0;
        if (every) kept.push_back(fit);
      }
      line.fits = std::move(kept);
      if (line.fits.empty()) return -1;
      for (size_t i = 0; i < line.cells.size(); ++i) {
        int seen = 0;
        for (const auto& fit : line.fits) seen |= 1 << fit[i];
        int& cell = domain[static_cast<size_t>(line.cells[i])];
        if (seen != cell) {
          cell = seen;
          changed = true;
        }
      }
    }
  }
  for (int bits : domain)
    if (bits & (bits - 1)) return -1;
  if (grid) {
    grid->assign(static_cast<size_t>(n * n), 0);
    for (size_t i = 0; i < domain.size(); ++i) (*grid)[i] = highestBit(domain[i]);
  }
  return passes - 1;
}

Puzzle generate(uint32_t seed, int n, int k, int minPasses, int maxPasses) {
  Rng random(seed);
  Puzzle p;
  p.n = n;
  p.k = k;
  p.blacks = n - k;
  for (int attempt = 1; attempt <= 20000; ++attempt) {
    std::vector<int> solution = randomGrid(random, n, k);
    std::vector<Clue> rows, cols;
    for (int r = 0; r < n; ++r) rows.push_back(clueOf(std::vector<int>(solution.begin() + r * n, solution.begin() + r * n + n)));
    for (int c = 0; c < n; ++c) {
      std::vector<int> col;
      for (int r = 0; r < n; ++r) col.push_back(solution[static_cast<size_t>(r * n + c)]);
      cols.push_back(clueOf(col));
    }
    const int passes = solve(n, k, rows, cols);
    if (passes < 0 || passes < minPasses || passes > maxPasses) continue;
    p.solution = std::move(solution);
    p.rows = std::move(rows);
    p.cols = std::move(cols);
    p.passes = passes;
    p.attempt = attempt;
    p.ok = true;
    return p;
  }
  return p;
}

std::string dayString(const Date& date) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", date.y, date.m, date.d);
  return buf;
}

bool parseDay(const std::string& day, Date* out) {
  Date d;
  if (std::sscanf(day.c_str(), "%d-%d-%d", &d.y, &d.m, &d.d) != 3 || d.m < 1 || d.m > 12 || d.d < 1 || d.d > 31) return false;
  if (out) *out = d;
  return true;
}

int puzzleNumber(const std::string& day) {
  Date a, b;
  if (!parseDay(day, &a) || !parseDay(kFirstDay, &b)) return 0;
  return static_cast<int>(daysFromCivil(a.y, a.m, a.d) - daysFromCivil(b.y, b.m, b.d)) + 1;
}

std::string dayBefore(const std::string& day) {
  Date a;
  if (!parseDay(day, &a)) return day;
  return dayString(civilFromDays(daysFromCivil(a.y, a.m, a.d) - 1));
}

int levelOf(const std::string& day) {
  Rng r(hash("niveau " + day));
  return kLevels[static_cast<int>(std::floor(r.next() * 3))];
}

Puzzle dailyPuzzle(const std::string& day) {
  const int level = levelOf(day);
  const bool hardest = level == kLevels[2];
  return generate(hash("sommetje " + day), 5, 4, level, hardest ? 99 : level);
}

int streak(const std::string& day, const std::function<bool(const std::string&)>& solvedOn) {
  int count = 0;
  for (std::string d = solvedOn(day) ? day : dayBefore(day); count < 100000 && solvedOn(d); d = dayBefore(d)) ++count;
  return count;
}

std::string streakText(int count) {
  return "Reeks: " + std::to_string(count) + (count == 1 ? " dag" : " dagen");
}

std::string clock(double seconds) {
  const long s = std::max(0L, static_cast<long>(std::floor(seconds)));
  const long h = s / 3600;
  const long m = (s % 3600) / 60;
  char buf[32];
  if (h)
    std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", h, m, s % 60);
  else
    std::snprintf(buf, sizeof buf, "%ld:%02ld", m, s % 60);
  return buf;
}

void noteMove(Trail& trail, int cell, int from, int to, bool slip) {
  if (to == from) return;
  if (from == kEmpty) {
    bool known = false;
    for (int c : trail.order) known = known || c == cell;
    if (!known) trail.order.push_back(cell);
  } else if (!slip) {
    trail.edits += 1;
    if (cell >= 0 && static_cast<size_t>(cell) < trail.changed.size()) trail.changed[static_cast<size_t>(cell)] = true;
  }
}

std::string marks(const Trail& trail) {
  std::string out;
  for (int cell : trail.order) {
    const bool changed = cell >= 0 && static_cast<size_t>(cell) < trail.changed.size() && trail.changed[static_cast<size_t>(cell)];
    out += changed ? 'y' : 'g';
  }
  return out;
}

std::string shareText(const std::string& name, double seconds, const Trail& trail, int n) {
  std::string text = name + " \xC2\xB7 " + clock(seconds);  // U+00B7 middle dot, as the web version
  const std::string m = marks(trail);
  for (size_t i = 0; i < m.size(); ++i) {
    if (i % static_cast<size_t>(n) == 0) text += '\n';
    text += m[i] == 'y' ? kYellow : kGreen;
  }
  return text;
}

}  // namespace pos::somdoku
