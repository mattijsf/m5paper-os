// Small string helpers shared by core, the platforms and the apps (one copy of trim / lower / split).
#pragma once

#include <string>
#include <vector>

namespace pos {

constexpr const char* kBlanks = " \t\r\n\v\f";

// `s` without the characters of `blanks` at its ends.
std::string trimmed(const std::string& s, const char* blanks = kBlanks);
// A-Z to a-z; other bytes (UTF-8 included) stay.
std::string lowerAscii(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
bool endsWith(const std::string& s, const std::string& suffix);

// The pieces of `s` between `sep`; empty pieces are skipped ("a,,b," -> a, b).
std::vector<std::string> split(const std::string& s, char sep);
std::string join(const std::vector<std::string>& parts, const char* sep);
// The same for whole numbers, as stored in an app's Store ("3,1,4").
std::vector<int> splitInts(const std::string& s, char sep = ',');
std::string joinInts(const std::vector<int>& values, const char* sep = ",");

}  // namespace pos
