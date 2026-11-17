#include "core/fs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <map>
#include <mutex>

namespace pos::fs {

namespace {

#if defined(POS_SIM)
std::string gSdRoot = "sim-data/sd";
std::string gDataRoot = "sim-data/data";
#else
std::string gSdRoot = "/sd";
std::string gDataRoot = "/littlefs";
#endif

std::atomic<unsigned> gChanges{0};  // changes(): bumped by writeFile, rename and removeAll on the card

// Only the card: /data writes (settings, saved state) are frequent, and nothing caches a /data file's contents this way.
void noteChange(const std::string& real) {
  if (!real.empty() && real.compare(0, gSdRoot.size(), gSdRoot) == 0) ++gChanges;
}

bool hasPrefix(const std::string& s, const char* prefix, size_t n) {
  return s.size() >= n && s.compare(0, n, prefix) == 0 && (s.size() == n || s[n] == '/');
}

// Puts the finished file `from` in the place of `to` (real paths). POSIX rename replaces atomically; FAT (the SD card)
// refuses to overwrite, so there the old file steps aside first and comes back if the new one cannot take its place:
// a failure never leaves the name without a file.
bool replaceReal(const std::string& from, const std::string& to) {
  if (std::rename(from.c_str(), to.c_str()) == 0) return true;
  struct stat st;
  if (stat(to.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;  // nothing in the way (the rename failed for another reason), or a folder
  const std::string aside = to + ".old";
  std::remove(aside.c_str());
  if (std::rename(to.c_str(), aside.c_str()) != 0) return false;
  if (std::rename(from.c_str(), to.c_str()) == 0) {
    std::remove(aside.c_str());
    return true;
  }
  std::rename(aside.c_str(), to.c_str());
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

// ---- small files of /data kept in memory
//
// Opening a file on the internal flash costs about 9 ms on the device, found or not (the launcher read 16 of them, two
// JSON files among them, every time an app closed: 170-190 ms before its first pixel, 2026-11-17). Everything that
// writes /data goes through this file, so what was read or written last is what the file holds: readFile() answers
// from memory, and every write, append, delete and rename here keeps that memory true. Only /data (the card can be
// taken out and changed elsewhere), only files up to kCachedFileMax, and "there is no such file" is remembered too.
constexpr size_t kCachedFileMax = 16 * 1024, kCachedTotalMax = 128 * 1024;

struct Cached {
  bool exists = false;
  std::string data;
};
std::mutex gCacheMutex;  // the worker reads and writes files too
std::map<std::string, Cached> gCached;  // by normalized virtual path
size_t gCachedBytes = 0;

bool cacheable(const std::string& key) { return key.compare(0, 6, "/data/") == 0; }

void cacheDropLocked(const std::string& key) {
  // The path itself and, when it is a folder, everything in it.
  for (auto it = gCached.lower_bound(key); it != gCached.end();) {
    const bool inside = it->first.size() > key.size() && it->first.compare(0, key.size(), key) == 0 && it->first[key.size()] == '/';
    if (it->first != key && !inside) {
      // Names like "/data/a.b" sort between "/data/a" and "/data/a/": look on while the prefix matches at all.
      if (it->first.compare(0, key.size(), key) != 0) break;
      ++it;
      continue;
    }
    gCachedBytes -= it->second.data.size();
    it = gCached.erase(it);
  }
}

void cacheDrop(const std::string& virtualPath) {
  const std::string key = normalize(virtualPath);
  if (!cacheable(key) && key != "/data") return;
  std::lock_guard<std::mutex> lock(gCacheMutex);
  cacheDropLocked(key);
}

void cachePut(const std::string& key, bool exists, const std::string& data) {
  if (!cacheable(key)) return;
  std::lock_guard<std::mutex> lock(gCacheMutex);
  cacheDropLocked(key);
  if (data.size() > kCachedFileMax) return;  // a big file is read from the flash each time
  if (gCachedBytes + data.size() > kCachedTotalMax) {  // full: start over (simple, and rare: /data holds little)
    gCached.clear();
    gCachedBytes = 0;
  }
  gCached[key] = Cached{exists, data};
  gCachedBytes += data.size();
}

// 1: the file is known and `out` holds it; 0: known to be missing; -1: not known.
int cacheGet(const std::string& key, std::string* out) {
  if (!cacheable(key)) return -1;
  std::lock_guard<std::mutex> lock(gCacheMutex);
  auto it = gCached.find(key);
  if (it == gCached.end()) return -1;
  if (it->second.exists && out) *out = it->second.data;
  return it->second.exists ? 1 : 0;
}

}  // namespace

void setRoots(const std::string& sdRoot, const std::string& dataRoot) {
  gSdRoot = sdRoot;
  gDataRoot = dataRoot;
  std::lock_guard<std::mutex> lock(gCacheMutex);  // other files now: nothing remembered is theirs
  gCached.clear();
  gCachedBytes = 0;
}

std::string normalize(const std::string& v) {
  if (v.empty() || v[0] != '/') return "";
  std::string out;
  for (size_t i = 1; i <= v.size();) {
    size_t j = v.find('/', i);
    if (j == std::string::npos) j = v.size();
    const std::string part = v.substr(i, j - i);
    if (part == "..") return "";
    if (!part.empty() && part != ".") out += "/" + part;
    i = j + 1;
  }
  return out;
}

std::string resolve(const std::string& virtualPath) {
  const std::string v = normalize(virtualPath);
  if (hasPrefix(v, "/sd", 3)) return gSdRoot + v.substr(3);
  if (hasPrefix(v, "/data", 5)) return gDataRoot + v.substr(5);
  return "";
}

bool exists(const std::string& v) {
  const int known = cacheGet(normalize(v), nullptr);
  if (known >= 0) return known == 1;
  std::string p = resolve(v);
  struct stat st;
  return !p.empty() && stat(p.c_str(), &st) == 0;
}

bool isDir(const std::string& v) {
  std::string p = resolve(v);
  struct stat st;
  return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool readFile(const std::string& v, std::string& out) {
  const std::string key = normalize(v);
  const int known = cacheGet(key, &out);
  if (known >= 0) return known == 1;
  std::string p = resolve(v);
  if (p.empty()) return false;
  FILE* f = std::fopen(p.c_str(), "rb");
  if (!f) {
    if (errno == ENOENT) cachePut(key, false, "");  // (any other failure may pass: ask the flash again next time)
    return false;
  }
  out.clear();
  char buf[1024];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  bool ok = !std::ferror(f);
  std::fclose(f);
  if (ok) cachePut(key, true, out);
  return ok;
}

bool writeFile(const std::string& v, const std::string& data) {
  std::string p = resolve(v);
  noteChange(p);  // it replaces the file (written aside, then renamed over it)
  if (p.empty()) return false;
  size_t slash = p.rfind('/');
  if (slash != std::string::npos && slash > 0 && !mkdirsReal(p.substr(0, slash))) return false;

  // One writer at a time: the worker and the UI thread may save the same file (the clipboard), and both would use <path>.tmp.
  static std::mutex writing;
  std::lock_guard<std::mutex> lock(writing);
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
  if (!replaceReal(tmp, p)) {
    std::remove(tmp.c_str());
    cacheDrop(v);  // whatever is there now: ask the flash
    return false;
  }
  cachePut(normalize(v), true, data);  // what the file holds from here on
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

unsigned changes() { return gChanges.load(); }

bool removeAll(const std::string& v) {
  std::string p = resolve(v);
  cacheDrop(v);
  noteChange(p);
  return !p.empty() && p != gSdRoot && p != gDataRoot && removeReal(p);
}

bool rename(const std::string& from, const std::string& to) {
  std::string a = resolve(from), b = resolve(to);
  cacheDrop(from);
  cacheDrop(to);
  noteChange(b);
  return !a.empty() && !b.empty() && replaceReal(a, b);
}

bool appendFile(const std::string& v, const std::string& data) {
  const std::string p = resolve(v);
  if (p.empty()) return false;
  cacheDrop(v);
  const size_t slash = p.rfind('/');
  if (slash != std::string::npos && slash > 0) mkdirsReal(p.substr(0, slash));
  FILE* f = std::fopen(p.c_str(), "ab");
  if (!f) return false;
  const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  return std::fclose(f) == 0 && ok;
}

bool fileInfo(const std::string& v, uint64_t& size, int64_t& mtime) {
  std::string p = resolve(v);
  struct stat st;
  if (p.empty() || stat(p.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) return false;
  size = static_cast<uint64_t>(st.st_size);
  mtime = static_cast<int64_t>(st.st_mtime);
  return true;
}

bool Writer::open(const std::string& v) {
  close();
  std::string p = resolve(v);
  if (p.empty()) return false;
  size_t slash = p.rfind('/');
  if (slash != std::string::npos && slash > 0 && !mkdirsReal(p.substr(0, slash))) return false;
  noteChange(p);
  cacheDrop(v);
  virtual_ = v;
  f_ = std::fopen(p.c_str(), "wb");
  ok_ = f_ != nullptr;
  path_ = p;
  pos_ = 0;
  retries_ = 0;
  if (f_) std::setvbuf(f_, nullptr, _IONBF, 0);  // each write reaches the file system now: a failure belongs to that piece
  return ok_;
}

bool Writer::write(const void* data, size_t n) {
  if (!f_ || !ok_) return false;
  if (std::fwrite(data, 1, n, f_) == n) {
    pos_ += n;
    return true;
  }
  // The file system marks the file broken after a disk error: reopen it and write the piece again where it belongs.
  for (int attempt = 0; attempt < 3; ++attempt) {
    std::fclose(f_);
    f_ = nullptr;
    std::this_thread::sleep_for(std::chrono::milliseconds(20 * (attempt + 1)));
    f_ = std::fopen(path_.c_str(), "r+b");
    if (!f_) continue;
    std::setvbuf(f_, nullptr, _IONBF, 0);
    if (std::fseek(f_, static_cast<long>(pos_), SEEK_SET) == 0 && std::fwrite(data, 1, n, f_) == n) {
      pos_ += n;
      ++retries_;
      return true;
    }
  }
  ok_ = false;
  return false;
}

bool Writer::close() {
  if (!f_) return ok_;
  ok_ = (std::fclose(f_) == 0) && ok_;
  f_ = nullptr;
  cacheDrop(virtual_);  // (someone may have read the half-written file meanwhile)
  return ok_;
}

bool Reader::open(const std::string& v) {
  close();
  const std::string p = resolve(v);
  if (p.empty()) return false;
  f_ = std::fopen(p.c_str(), "rb");
  return f_ != nullptr;
}

bool Reader::readAt(uint64_t offset, void* data, size_t n) {
  if (!f_ || offset > static_cast<uint64_t>(LONG_MAX)) return false;
  if (std::fseek(f_, static_cast<long>(offset), SEEK_SET) != 0) return false;
  return std::fread(data, 1, n, f_) == n;
}

void Reader::close() {
  if (f_) std::fclose(f_);
  f_ = nullptr;
}

bool mkdirs(const std::string& v) {
  std::string p = resolve(v);
  return !p.empty() && mkdirsReal(p);
}

}  // namespace pos::fs
