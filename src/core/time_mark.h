// A moment remembered in a small file: "something is still owed since then" (a clipboard copy the phone has not got, the
// `paired` envelope). The file holds the unix time as one decimal number; a missing or damaged file is "no mark".
#pragma once

#include <cstdint>
#include <string>

namespace pos {

class TimeMark {
 public:
  explicit TimeMark(std::string virtualPath) : path_(std::move(virtualPath)) {}
  void set(int64_t unixSeconds) const;  // negative times are stored as 0
  bool get(int64_t& unixSeconds) const;
  void clear() const;

 private:
  std::string path_;
};

}  // namespace pos
