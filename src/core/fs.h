// File access through virtual paths. Shared code never uses real paths:
//   /sd/...    microSD         device: /sd         simulator: sim-data/sd
//   /data/...  internal flash  device: /littlefs   simulator: sim-data/data
// Both platforms expose POSIX file APIs, so no HAL class is needed (docs/ARCHITECTURE.md).
// Blocking I/O: do not call on the UI thread for big files (use ctx.async).
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pos::fs {

struct DirEntry {
  std::string name;  // file or folder name, no path
  bool isDir = false;
  uint64_t size = 0;  // bytes, 0 for folders
};

// Maps a virtual path to a real one. Returns "" for anything that is not under /sd or /data,
// or that contains a ".." component.
std::string resolve(const std::string& virtualPath);

// Override the real root folders (the defaults depend on the platform). For tests.
void setRoots(const std::string& sdRoot, const std::string& dataRoot);

bool exists(const std::string& virtualPath);
bool readFile(const std::string& virtualPath, std::string& out);
// Appends to a file (created, with its folders, if missing).
bool appendFile(const std::string& virtualPath, const std::string& data);
// Atomic: writes <path>.tmp, then renames over <path>. Creates missing parent folders.
bool writeFile(const std::string& virtualPath, const std::string& data);
// Entries sorted by name, without "." and "..". False if the folder cannot be opened.
bool listDir(const std::string& virtualPath, std::vector<DirEntry>& out);
// Deletes a file, or a folder and everything in it. True if nothing is left afterwards (also if it never existed).
bool removeAll(const std::string& virtualPath);
// Renames a file, replacing the destination. Both must be virtual paths.
bool rename(const std::string& fromVirtual, const std::string& toVirtual);
// Size and modification time (unix seconds) of a file; false if it does not exist.
bool fileInfo(const std::string& virtualPath, uint64_t& size, int64_t& mtime);

// Sequential writer (large files): open, write chunks, close. Parent folders are created.
class Writer {
 public:
  ~Writer() { close(); }
  bool open(const std::string& virtualPath);
  bool write(const void* data, size_t n);
  bool close();  // true if everything reached the file
  bool isOpen() const { return f_ != nullptr; }

 private:
  FILE* f_ = nullptr;
  bool ok_ = true;
};

// Creates the folder and every missing parent ("mkdir -p"). True if it exists afterwards.
bool mkdirs(const std::string& virtualPath);

}  // namespace pos::fs
