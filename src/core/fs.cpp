#include "core/fs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>

namespace pos::fs {

namespace {

#if defined(POS_SIM)
std::string gSdRoot = "sim-data/sd";
std::string gDataRoot = "sim-data/data";
#else
std::string gSdRoot = "/sd";
std::string gDataRoot = "/littlefs";
#endif

bool hasPrefix(const std::string& s, const char* prefix, size_t n) {
  return s.size() >= n && s.compare(0, n, prefix) == 0 && (s.size() == n || s[n] == '/');
}

bool hasDotDot(const std::string& p) {
  size_t i = 0;
  while (i <= p.size()) {
    size_t j = p.find('/', i);
    if (j == std::string::npos) j = p.size();
    if (p.compare(i, j - i, "..") == 0) return true;
    i = j + 1;
  }
  return false;
}

// mkdir -p on a real path.
bool mkdirsReal(const std::string& path) {
  if (path.empty()) return false;
  struct stat st;
  if (stat(path.c_str(), &st) == 0) return S_ISDIR(st.st_mode);
  for (size_t i = 1; i < path.size(); ++i) {
    if (path[i] != '/') continue;
    std::string part = path.substr(0, i);
    if (mkdir(part.c_str(), 0775) != 0 && errno != EEXIST) return false;
  }
  return mkdir(path.c_str(), 0775) == 0 || errno == EEXIST;
}

}  // namespace

void setRoots(const std::string& sdRoot, const std::string& dataRoot) {
  gSdRoot = sdRoot;
  gDataRoot = dataRoot;
}

std::string resolve(const std::string& v) {
  if (hasDotDot(v)) return "";
  if (hasPrefix(v, "/sd", 3)) return gSdRoot + v.substr(3);
  if (hasPrefix(v, "/data", 5)) return gDataRoot + v.substr(5);
  return "";
}

bool exists(const std::string& v) {
  std::string p = resolve(v);
  struct stat st;
  return !p.empty() && stat(p.c_str(), &st) == 0;
}

bool readFile(const std::string& v, std::string& out) {
  std::string p = resolve(v);
  if (p.empty()) return false;
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[1024];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  bool ok = !std::ferror(f);
  std::fclose(f);
  return ok;
}

bool writeFile(const std::string& v, const std::string& data) {
  std::string p = resolve(v);
  if (p.empty()) return false;
  size_t slash = p.rfind('/');
  if (slash != std::string::npos && slash > 0 && !mkdirsReal(p.substr(0, slash))) return false;

  std::string tmp = p + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = (std::fflush(f) == 0) && ok;
  ok = (std::fclose(f) == 0) && ok;
  if (!ok) {
    std::remove(tmp.c_str());
    return false;
  }
  // POSIX rename replaces atomically. FAT (SD card) may refuse to overwrite: remove, then retry.
  if (std::rename(tmp.c_str(), p.c_str()) != 0) {
    std::remove(p.c_str());
    if (std::rename(tmp.c_str(), p.c_str()) != 0) {
      std::remove(tmp.c_str());
      return false;
    }
  }
  return true;
}

bool listDir(const std::string& v, std::vector<DirEntry>& out) {
  std::string p = resolve(v);
  if (p.empty()) return false;
  DIR* d = opendir(p.c_str());
  if (!d) return false;
  out.clear();
  while (dirent* e = readdir(d)) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    DirEntry de;
    de.name = name;
    struct stat st;
    if (stat((p + "/" + name).c_str(), &st) == 0) {
      de.isDir = S_ISDIR(st.st_mode);
      de.size = de.isDir ? 0 : static_cast<uint64_t>(st.st_size);
    }
    out.push_back(de);
  }
  closedir(d);
  std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return true;
}

namespace {
bool removeReal(const std::string& p) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return true;
  if (S_ISDIR(st.st_mode)) {
    if (DIR* d = opendir(p.c_str())) {
      std::vector<std::string> names;
      while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
      }
      closedir(d);
      for (auto& n : names) removeReal(p + "/" + n);
    }
    return rmdir(p.c_str()) == 0;
  }
  return std::remove(p.c_str()) == 0;
}
}  // namespace

bool removeAll(const std::string& v) {
  std::string p = resolve(v);
  return !p.empty() && p != gSdRoot && p != gDataRoot && removeReal(p);
}

bool mkdirs(const std::string& v) {
  std::string p = resolve(v);
  return !p.empty() && mkdirsReal(p);
}

}  // namespace pos::fs
