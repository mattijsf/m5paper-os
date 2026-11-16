// Word game game logic (src/apps/wordgame/wordgame.h): marks, the day's word, the word list, the shared score, statistics.
#include <doctest.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "apps/wordgame/wordgame.h"

using namespace pos::wordgame;

TEST_CASE("wordgame: marks of a guess") {
  CHECK(marksOf("kamer", "kamer") == "ccccc");
  CHECK(marksOf("kamer", "blind") == "aaaaa");
  CHECK(marksOf("kamer", "merel") == "papca");  // the word has one e: the one at the right place is marked
  // A letter that is in the word once is marked once, and the right place wins over an earlier wrong place.
  CHECK(marksOf("boter", "tenen") == "paaca");
  CHECK(marksOf("appel", "papje") == "ppcap");
  // Two of a letter in the word: two of them can be marked, the third is not.
  CHECK(marksOf("sloot", "otter") == "ppaaa");
  CHECK(marksOf("sloot", "boord") == "apcaa");
  CHECK(marksOf("sloot", "motto") == "appap");
}

TEST_CASE("wordgame: the keys show the best mark of each letter") {
  const std::vector<std::string> guesses = {"tenen", "motor"};
  const auto keys = keyMarks("boter", guesses);
  CHECK(keys['t' - 'a'] == kRight);   // other place in tenen, right place in motor
  CHECK(keys['e' - 'a'] == kRight);   // absent at one place, right at another
  CHECK(keys['n' - 'a'] == kAbsent);
  CHECK(keys['o' - 'a'] == kRight);
  CHECK(keys['m' - 'a'] == kAbsent);
  CHECK(keys['r' - 'a'] == kRight);
  CHECK(keys['b' - 'a'] == kUnused);
  CHECK(keyMarks("boter", {})['b' - 'a'] == kUnused);
}

TEST_CASE("wordgame: solved and finished") {
  CHECK_FALSE(finished("kamer", {}));
  CHECK(solved("kamer", {"blind", "kamer"}));
  CHECK(finished("kamer", {"blind", "kamer"}));
  const std::vector<std::string> six(6, "blind");
  CHECK_FALSE(solved("kamer", six));
  CHECK(finished("kamer", six));
  CHECK_FALSE(finished("kamer", std::vector<std::string>(5, "blind")));
}

TEST_CASE("wordgame: days and puzzle numbers") {
  CHECK(dayOf({2026, 11, 16}) == 0);
  CHECK(puzzleNumber(0) == 1);
  CHECK(dayOf({2026, 11, 17}) == 1);
  CHECK(dayOf({2026, 11, 15}) == -1);
  CHECK(dayOf({2026, 12, 1}) == 15);
  CHECK(dayOf({2027, 1, 1}) == 46);
  CHECK(dayOf({2027, 11, 16}) == 365);
  CHECK(dayOf({2028, 2, 29}) == 470);
  CHECK(dayOf({2028, 3, 1}) == 471);
  CHECK(dayOf({2028, 11, 16}) == 731);  // 2028 is a leap year
}

TEST_CASE("wordgame: the word list") {
  REQUIRE(wordCount() > 2000);
  REQUIRE(answerCount() > 500);
  CHECK(std::strlen(data::kWords) == static_cast<size_t>(wordCount()) * kLen);
  // Sorted, no word twice, only a-z: the binary search depends on it.
  for (int i = 0; i < wordCount(); ++i) {
    const std::string w(data::kWords + i * kLen, kLen);
    for (char c : w) REQUIRE((c >= 'a' && c <= 'z'));
    if (i > 0) REQUIRE(std::string(data::kWords + (i - 1) * kLen, kLen) < w);
    REQUIRE(isWord(w));
  }
  CHECK_FALSE(isWord("xxxxx"));
  CHECK_FALSE(isWord("kame"));
  CHECK_FALSE(isWord("kamers"));
  CHECK_FALSE(isWord("KAMER"));
  CHECK_FALSE(isWord(""));
  CHECK(isWord("kamer"));
  CHECK(isWord("lopen"));
}

TEST_CASE("wordgame: one word a day, each answer once per round") {
  std::set<std::string> seen;
  for (int day = 0; day < answerCount(); ++day) {
    const std::string w = answerOf(day);
    REQUIRE(w.size() == static_cast<size_t>(kLen));
    REQUIRE(isWord(w));
    seen.insert(w);
  }
  CHECK(seen.size() == static_cast<size_t>(answerCount()));
  // The list wraps, also before the first day (a clock that is behind).
  CHECK(answerOf(answerCount()) == answerOf(0));
  CHECK(answerOf(answerCount() + 7) == answerOf(7));
  CHECK(answerOf(-1) == answerOf(answerCount() - 1));
  CHECK(answerOf(-answerCount()) == answerOf(0));
  // Not in alphabetical order: neighbours differ in their first letter most of the time.
  int sameFirst = 0;
  for (int day = 1; day < 100; ++day) sameFirst += answerOf(day)[0] == answerOf(day - 1)[0];
  CHECK(sameFirst < 30);
}

TEST_CASE("wordgame: the shared score") {
  // Name (the one constant), number, tries; an empty line; a line of squares per guess; nothing after the last line.
  const std::string name = kName;
  CHECK(shareText(12, "boter", {"tenen", "motor", "boter"}) ==
        name + " 12 3/6\n"
        "\n"
        "\xF0\x9F\x9F\xA8\xE2\xAC\x9B\xE2\xAC\x9B\xF0\x9F\x9F\xA9\xE2\xAC\x9B\n"
        "\xE2\xAC\x9B\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xE2\xAC\x9B\xF0\x9F\x9F\xA9\n"
        "\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9");
  CHECK(shareText(1, "kamer", {"kamer"}) == name + " 1 1/6\n\n\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9\xF0\x9F\x9F\xA9");
  const std::string lost = shareText(7, "kamer", std::vector<std::string>(6, "blind"));
  CHECK(lost.rfind(name + " 7 X/6\n\n", 0) == 0);
  CHECK(lost.back() != '\n');
  int lines = 0;
  for (char c : lost) lines += c == '\n';
  CHECK(lines == 7);  // the title, the empty line and five line ends between six rows
}

TEST_CASE("wordgame: statistics and the streak") {
  Stats s;
  CHECK(winPercent(s) == 0);
  CHECK(streakOn(s, 0) == 0);
  record(s, 10, true, 4);
  record(s, 11, true, 2);
  record(s, 12, true, 4);
  CHECK(s.played == 3);
  CHECK(s.won == 3);
  CHECK(s.streak == 3);
  CHECK(s.maxStreak == 3);
  CHECK(s.turns[3] == 2);
  CHECK(s.turns[1] == 1);
  CHECK(streakOn(s, 12) == 3);
  CHECK(streakOn(s, 13) == 3);  // not played yet today: the streak still stands
  CHECK(streakOn(s, 14) == 0);  // a day was missed
  record(s, 13, false, 6);
  CHECK(s.played == 4);
  CHECK(s.won == 3);
  CHECK(streakOn(s, 13) == 0);
  CHECK(streakOn(s, 14) == 0);
  CHECK(s.maxStreak == 3);
  CHECK(winPercent(s) == 75);
  record(s, 20, true, 6);
  CHECK(s.streak == 1);  // after a gap the streak starts again
  CHECK(s.turns[5] == 1);
  CHECK(s.maxStreak == 3);
  Stats third;
  record(third, 0, true, 3);
  record(third, 1, false, 6);
  record(third, 2, false, 6);
  CHECK(winPercent(third) == 33);
}

TEST_CASE("wordgame: the fetched list: only the second part, packed and sorted") {
  // Part 1 (another game's words of the day) is never taken, whatever is in it.
  const std::string text = "eerst\ntwede\n\nzomer\nappel\n  kamer \r\nappel\nKAMER\nboot\nlangerwoord\ntjalk\nab1de\n";
  const std::string packed = packGuessList(text);
  CHECK(packed == "appelkamertjalkzomer");
  CHECK(validPacked(packed));
  CHECK(inPacked(packed, "tjalk"));
  CHECK(inPacked(packed, "appel"));
  CHECK(inPacked(packed, "zomer"));
  CHECK_FALSE(inPacked(packed, "eerst"));
  CHECK_FALSE(inPacked(packed, "twede"));
  CHECK_FALSE(inPacked(packed, "boot"));
  CHECK_FALSE(inPacked(packed, ""));
  CHECK_FALSE(inPacked("", "appel"));
  // Windows line ends, no line end after the last word, empty lines before the first part.
  CHECK(packGuessList("\r\n\r\neerst\r\n\r\nzomer\r\nappel") == "appelzomer");
  // No second part: nothing, also for an error page.
  CHECK(packGuessList("eerst\ntwede\n") == "");
  CHECK(packGuessList("") == "");
  CHECK(packGuessList("<html>Not found</html>") == "");
  CHECK(packGuessList("eerst\n\n") == "");
}

TEST_CASE("wordgame: a damaged cache file is not used") {
  CHECK(validPacked("appelkamer"));
  CHECK_FALSE(validPacked(""));
  CHECK_FALSE(validPacked("appelkame"));     // cut
  CHECK_FALSE(validPacked("kamerappel"));    // not sorted
  CHECK_FALSE(validPacked("appelappel"));    // twice
  CHECK_FALSE(validPacked("appelKamer"));
  CHECK_FALSE(validPacked(std::string("appel\0amer", 10)));
}

TEST_CASE("wordgame: when the list is fetched") {
  const int64_t now = 1830000000;
  CHECK(listDue(now, 0, 0));                                    // never fetched
  CHECK_FALSE(listDue(now, now - 6 * 86400, 0));                // fresh
  CHECK(listDue(now, now - 7 * 86400, 0));                      // a week old
  CHECK_FALSE(listDue(now, now - 30 * 86400, now - 3600));      // old, but tried an hour ago
  CHECK(listDue(now, now - 30 * 86400, now - 86400));           // tried a day ago
  CHECK_FALSE(listDue(now, 0, now - 60));                       // never fetched, tried a minute ago
  CHECK(listDue(now, now + 86400 * 400, 0));                    // fetched "in the future": the clock was wrong then
  CHECK(listDue(now, now - 8 * 86400, now + 5000));             // tried "in the future": ignored
  // No clock: only when there is no list at all.
  CHECK(listDue(0, 0, 0));
  CHECK(listDue(0, 0, 12345));
  CHECK_FALSE(listDue(0, now, 0));
}

