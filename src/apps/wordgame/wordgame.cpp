#include "apps/wordgame/wordgame.h"

#include <algorithm>
#include <cstring>

namespace pos::wordgame {

namespace {

// Days since 1970-01-01 of a civil date (proleptic Gregorian).
long daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const long era = (y >= 0 ? y : y - 399) / 400;
  const long yoe = y - era * 400;
  const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

const long kFirstDay = 20773;  // 2026-11-16

int rank(char mark) { return mark == kRight ? 3 : mark == kOther ? 2 : mark == kAbsent ? 1 : 0; }

}  // namespace

int dayOf(const Date& date) { return static_cast<int>(daysFromCivil(date.year, date.month, date.day) - kFirstDay); }

int puzzleNumber(int day) { return day + 1; }

int answerCount() { return data::kAnswerCount; }
int wordCount() { return data::kWordCount; }

std::string answerOf(int day) {
  const int n = data::kAnswerCount;
  if (n <= 0) return "";
  const int i = ((day % n) + n) % n;
  return std::string(data::kWords + static_cast<size_t>(data::kAnswers[i]) * kLen, kLen);
}

bool isWord(const std::string& word) {
  if (word.size() != static_cast<size_t>(kLen)) return false;
  int lo = 0, hi = data::kWordCount - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const int c = std::memcmp(word.data(), data::kWords + static_cast<size_t>(mid) * kLen, kLen);
    if (c == 0) return true;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return false;
}

std::string marksOf(const std::string& answer, const std::string& guess) {
  std::string marks(guess.size(), kAbsent);
  int left[26] = {};
  for (size_t i = 0; i < answer.size(); ++i) {
    if (i < guess.size() && guess[i] == answer[i])
      marks[i] = kRight;
    else if (answer[i] >= 'a' && answer[i] <= 'z')
      ++left[answer[i] - 'a'];
  }
  for (size_t i = 0; i < guess.size(); ++i) {
    if (marks[i] == kRight || guess[i] < 'a' || guess[i] > 'z') continue;
    int& n = left[guess[i] - 'a'];
    if (n > 0) {
      marks[i] = kOther;
      --n;
    }
  }
  return marks;
}

std::array<char, 26> keyMarks(const std::string& answer, const std::vector<std::string>& guesses) {
  std::array<char, 26> keys;
  keys.fill(kUnused);
  for (const std::string& g : guesses) {
    const std::string m = marksOf(answer, g);
    for (size_t i = 0; i < g.size(); ++i) {
      if (g[i] < 'a' || g[i] > 'z') continue;
      char& k = keys[static_cast<size_t>(g[i] - 'a')];
      if (rank(m[i]) > rank(k)) k = m[i];
    }
  }
  return keys;
}

bool solved(const std::string& answer, const std::vector<std::string>& guesses) {
  return !guesses.empty() && guesses.back() == answer;
}

bool finished(const std::string& answer, const std::vector<std::string>& guesses) {
  return solved(answer, guesses) || guesses.size() >= static_cast<size_t>(kTries);
}

std::string shareText(int number, const std::string& answer, const std::vector<std::string>& guesses) {
  std::string out = std::string(kName) + " " + std::to_string(number) + " " +
                    (solved(answer, guesses) ? std::to_string(guesses.size()) : std::string("X")) + "/" + std::to_string(kTries) + "\n";
  for (const std::string& g : guesses) {
    out += "\n";
    for (char m : marksOf(answer, g)) out += m == kRight ? "\xF0\x9F\x9F\xA9" : m == kOther ? "\xF0\x9F\x9F\xA8" : "\xE2\xAC\x9B";
  }
  return out;
}

void record(Stats& stats, int day, bool won, int tries) {
  ++stats.played;
  if (!won) {
    stats.streak = 0;
    return;
  }
  ++stats.won;
  if (tries >= 1 && tries <= kTries) ++stats.turns[static_cast<size_t>(tries - 1)];
  stats.streak = stats.lastWonDay == day - 1 ? stats.streak + 1 : 1;
  stats.lastWonDay = day;
  stats.maxStreak = std::max(stats.maxStreak, stats.streak);
}

int streakOn(const Stats& stats, int today) {
  return stats.lastWonDay == today || stats.lastWonDay == today - 1 ? stats.streak : 0;
}

std::string packGuessList(const std::string& text) {
  // The second part starts after the first empty line (a line of only spaces or a carriage return counts as empty).
  size_t pos = 0;
  bool found = false, any = false;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    bool blank = true;
    for (size_t i = pos; i < end; ++i) blank = blank && (text[i] == ' ' || text[i] == '\r' || text[i] == '\t');
    pos = end + 1;
    if (blank && any) {
      found = true;
      break;
    }
    any = any || !blank;
  }
  if (!found) return "";
  // Words as numbers (26^5 fits in 32 bits): 4 bytes a word while sorting, instead of a string each.
  std::vector<uint32_t> codes;
  while (pos < text.size() && codes.size() < kListMaxWords) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    size_t a = pos, b = end;
    while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\r' || text[b - 1] == '\t')) --b;
    pos = end + 1;
    if (b - a != static_cast<size_t>(kLen)) continue;
    uint32_t code = 0;
    bool ok = true;
    for (size_t i = a; i < b; ++i) {
      ok = ok && text[i] >= 'a' && text[i] <= 'z';
      code = code * 26 + static_cast<uint32_t>(text[i] - 'a');
    }
    if (ok) codes.push_back(code);
  }
  std::sort(codes.begin(), codes.end());
  codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
  std::string packed(codes.size() * kLen, 'a');
  for (size_t w = 0; w < codes.size(); ++w) {
    uint32_t code = codes[w];
    for (int i = kLen - 1; i >= 0; --i) {
      packed[w * kLen + static_cast<size_t>(i)] = static_cast<char>('a' + code % 26);
      code /= 26;
    }
  }
  return packed;
}

bool validPacked(const std::string& packed) {
  if (packed.empty() || packed.size() % kLen != 0 || packed.size() > kListMaxWords * kLen) return false;
  for (size_t i = 0; i < packed.size(); ++i) {
    if (packed[i] < 'a' || packed[i] > 'z') return false;
    if (i >= static_cast<size_t>(kLen) && i % kLen == 0 && std::memcmp(packed.data() + i - kLen, packed.data() + i, kLen) >= 0) return false;
  }
  return true;
}

bool inPacked(const std::string& packed, const std::string& word) {
  if (word.size() != static_cast<size_t>(kLen)) return false;
  long lo = 0, hi = static_cast<long>(packed.size() / kLen) - 1;
  while (lo <= hi) {
    const long mid = (lo + hi) / 2;
    const int c = std::memcmp(word.data(), packed.data() + static_cast<size_t>(mid) * kLen, kLen);
    if (c == 0) return true;
    if (c < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return false;
}

bool listDue(int64_t now, int64_t fetchedAt, int64_t triedAt) {
  if (now <= 1700000000) return fetchedAt == 0;  // no clock: once, so the game gets its words (the connection sets the clock)
  if (triedAt != 0 && now >= triedAt && now - triedAt < kListRetryS) return false;
  return fetchedAt == 0 || now < fetchedAt || now - fetchedAt >= kListMaxAgeS;
}

int winPercent(const Stats& stats) { return stats.played > 0 ? (stats.won * 100 + stats.played / 2) / stats.played : 0; }

}  // namespace pos::wordgame
