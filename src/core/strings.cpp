#include "core/strings.h"

#include <cstdlib>

namespace pos {

std::string trimmed(const std::string& s, const char* blanks) {
  const size_t a = s.find_first_not_of(blanks);
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(blanks) - a + 1);
}

std::string lowerAscii(std::string s) {
  for (char& c : s)
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  return s;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

bool endsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  for (size_t i = 0; i < s.size();) {
    size_t j = s.find(sep, i);
    if (j == std::string::npos) j = s.size();
    if (j > i) out.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

std::string join(const std::vector<std::string>& parts, const char* sep) {
  std::string out;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) out += sep;
    out += parts[i];
  }
  return out;
}

std::vector<int> splitInts(const std::string& s, char sep) {
  std::vector<int> out;
  for (const std::string& piece : split(s, sep)) out.push_back(std::atoi(piece.c_str()));
  return out;
}

std::string joinInts(const std::vector<int>& values, const char* sep) {
  std::string out;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) out += sep;
    out += std::to_string(values[i]);
  }
  return out;
}

}  // namespace pos
