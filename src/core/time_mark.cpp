#include "core/time_mark.h"

#include "core/fs.h"

namespace pos {

void TimeMark::set(int64_t unixSeconds) const { fs::writeFile(path_, std::to_string(unixSeconds < 0 ? 0 : unixSeconds)); }

bool TimeMark::get(int64_t& unixSeconds) const {
  std::string s;
  if (!fs::readFile(path_, s) || s.empty() || s.size() > 18) return false;  // 18 digits fit an int64_t
  int64_t v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  unixSeconds = v;
  return true;
}

void TimeMark::clear() const { fs::removeAll(path_); }

}  // namespace pos
