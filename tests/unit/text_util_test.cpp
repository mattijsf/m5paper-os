// The shared text helpers: core/utf8, core/strings, core/timefmt (dates and their text forms).
#include <doctest.h>

#include <string>
#include <vector>

#include "core/strings.h"
#include "core/timefmt.h"
#include "core/utf8.h"

using namespace pos;

namespace {
uint32_t decodeOne(const std::string& s, size_t* used = nullptr) {
  uint32_t cp = 0;
  const size_t n = utf8::decode(s, 0, cp);
  if (used) *used = n;
  return cp;
}
}  // namespace

TEST_CASE("utf8: decode is strict") {
  size_t n = 0;
  CHECK(decodeOne("a", &n) == 'a');
  CHECK(n == 1);
  CHECK(decodeOne("\xC3\xA9", &n) == 0xE9);
  CHECK(n == 2);
  CHECK(decodeOne("\xE2\x82\xAC", &n) == 0x20AC);
  CHECK(n == 3);
  CHECK(decodeOne("\xF0\x9F\x98\x80", &n) == 0x1F600);
  CHECK(n == 4);
  CHECK(decodeOne("\x80", &n) == utf8::kInvalid);      // a stray continuation byte
  CHECK(n == 1);
  CHECK(decodeOne("\xE2\x82", &n) == utf8::kInvalid);  // cut off
  CHECK(n == 1);
  CHECK(decodeOne("\xC0\xAF") == utf8::kInvalid);      // overlong '/'
  CHECK(decodeOne("\xE0\x80\xAF") == utf8::kInvalid);
  CHECK(decodeOne("\xED\xA0\x80") == utf8::kInvalid);  // a surrogate
  CHECK(decodeOne("\xF4\x90\x80\x80") == utf8::kInvalid);  // beyond U+10FFFF
  CHECK(decodeOne("\xFF") == utf8::kInvalid);
}

TEST_CASE("utf8: append, valid, round trip") {
  for (uint32_t cp : {0x41u, 0xE9u, 0x7FFu, 0x800u, 0x20ACu, 0xFFFFu, 0x10000u, 0x1F600u, 0x10FFFFu}) {
    std::string s;
    utf8::append(s, cp);
    CHECK(utf8::valid(s));
    size_t n = 0;
    CHECK(decodeOne(s, &n) == cp);
    CHECK(n == s.size());
  }
  std::string bad;
  utf8::append(bad, 0);
  utf8::append(bad, 0xD800);
  utf8::append(bad, 0x110000);
  CHECK(bad == "\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD");
  CHECK(utf8::valid(""));
  CHECK(utf8::valid("caf\xC3\xA9 \xE2\x82\xAC"));
  CHECK_FALSE(utf8::valid("caf\xE9"));  // Latin-1
  CHECK_FALSE(utf8::valid("\xC0\xAF"));
}

TEST_CASE("utf8: length, prefix, truncate, lastStart, span") {
  const std::string s = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";  // a é € 😀 z
  CHECK(utf8::length(s) == 5);
  CHECK(utf8::length("") == 0);
  CHECK(utf8::prefix(s, 0) == "");
  CHECK(utf8::prefix(s, 2) == "a\xC3\xA9");
  CHECK(utf8::prefix(s, 4) == "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
  CHECK(utf8::prefix(s, 99) == s);
  CHECK(utf8::truncate("abc", 10) == "abc");
  CHECK(utf8::truncate("abc", 2) == "ab");
  CHECK(utf8::truncate("a\xE2\x82\xAC", 1) == "a");
  CHECK(utf8::truncate("a\xE2\x82\xAC", 2) == "a");
  CHECK(utf8::truncate("a\xE2\x82\xAC", 3) == "a");
  CHECK(utf8::truncate("a\xE2\x82\xAC", 4) == "a\xE2\x82\xAC");
  CHECK(utf8::truncate("\xE2\x82\xAC", 0) == "");
  CHECK(utf8::lastStart("") == 0);
  CHECK(utf8::lastStart("ab") == 1);
  CHECK(utf8::lastStart("a\xE2\x82\xAC") == 1);
  CHECK(utf8::span(s, 0) == 1);
  CHECK(utf8::span(s, 1) == 2);
  CHECK(utf8::span(s, 3) == 3);
  CHECK(utf8::span(s, 6) == 4);
  CHECK(utf8::span("\xE2\x82", 0) == 2);  // cut off: what is there
}

TEST_CASE("strings: trimmed, lowerAscii, startsWith, endsWith") {
  CHECK(trimmed("  hi \r\n") == "hi");
  CHECK(trimmed("\t\n ") == "");
  CHECK(trimmed("") == "");
  CHECK(trimmed("a b") == "a b");
  CHECK(trimmed(" a\n", " ") == "a\n");
  CHECK(lowerAscii("AbC.EPUB") == "abc.epub");
  CHECK(lowerAscii("\xC3\x89t\xC3\xA9") == "\xC3\x89t\xC3\xA9");  // UTF-8 stays
  CHECK(startsWith("Basic abc", "Basic "));
  CHECK_FALSE(startsWith("Ba", "Basic "));
  CHECK(startsWith("x", ""));
  CHECK(endsWith("book.epub", ".epub"));
  CHECK_FALSE(endsWith("pub", ".epub"));
}

TEST_CASE("strings: split and join") {
  CHECK(split("a,,b,", ',') == std::vector<std::string>{"a", "b"});
  CHECK(split("", ',').empty());
  CHECK(split("one", ',') == std::vector<std::string>{"one"});
  CHECK(join({"a", "b", "c"}, ",") == "a,b,c");
  CHECK(join({}, ",") == "");
  CHECK(splitInts("3,1,,-4") == std::vector<int>{3, 1, -4});
  CHECK(joinInts({3, 1, -4}) == "3,1,-4");
  CHECK(splitInts(joinInts({})).empty());
}

TEST_CASE("timefmt: day numbers") {
  CHECK(daysFromCivil(1970, 1, 1) == 0);
  CHECK(daysFromCivil(2026, 11, 16) == 20773);
  CHECK(civilFromDays(20773) == Date{2026, 11, 16});
  CHECK(weekdayOf(0) == 4);      // 1970-01-01 was a Thursday
  CHECK(weekdayOf(20773) == 1);  // 2026-11-16 is a Monday
  CHECK(weekdayOf(-1) == 3);
  for (int d = -800; d < 60000; d += 41) CHECK(daysFromCivil(civilFromDays(d)) == d);
  CHECK(isoDate(Date{2028, 2, 29}) == "2028-02-29");
  CHECK(isoDate(0) == "1970-01-01");
  CHECK_FALSE(clockIsSet(0));
  CHECK_FALSE(clockIsSet(kClockSetAfter));
  CHECK(clockIsSet(kClockSetAfter + 1));
}

TEST_CASE("timefmt: text") {
  CHECK(hhmm(0) == "00:00");
  CHECK(hhmm(9 * 60 + 5) == "09:05");
  CHECK(hhmm(23 * 60 + 59) == "23:59");
  CHECK(hhmm(1440) == "24:00");  // an event that ends at midnight
  CHECK(hhmm(1441) == "00:01");
  CHECK(std::string(weekdayName(0)) == "Sunday");
  CHECK(std::string(monthName(11)) == "December");
  CHECK(weekdayShort(3) == "Wed");
  CHECK(monthShort(8) == "Sep");
  applyTimezone("UTC0");
  CHECK(clockText(0) == "");
  CHECK(clockText(1768480440) == "12:34");  // 2026-01-15 12:34:00 UTC
  int day = 0;
  CHECK_FALSE(localDay(0, day));
  CHECK(localDay(1768480440, day));
  CHECK(day == daysFromCivil(2026, 1, 15));
}
