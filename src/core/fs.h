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

// A virtual path in its one spelling: "/sd/books/" and "/sd//books/./" are "/sd/books". "" for a path that is not
// absolute or has a ".." part. Compare paths (a folder that must not be deleted) only in this form.
std::string normalize(const std::string& virtualPath);
// Maps a virtual path to a real one. Returns "" for anything that is not under /sd or /data,
// or that contains a ".." component.
std::string resolve(const std::string& virtualPath);

// Override the real root folders (the defaults depend on the platform). For tests.
void setRoots(const std::string& sdRoot, const std::string& dataRoot);

bool exists(const std::string& virtualPath);
bool isDir(const std::string& virtualPath);  // an existing folder (one stat; listDir reads the whole folder)
bool readFile(const std::string& virtualPath, std::string& out);
// Appends to a file (created, with its folders, if missing).
bool appendFile(const std::string& virtualPath, const std::string& data);
// Writes <path>.tmp, then puts it in the place of <path>: the old contents stay until the new file is complete, and a
// failure leaves them (atomic on LittleFS; on the card's FAT the old file steps aside as <path>.old for a moment).
// Creates missing parent folders.
bool writeFile(const std::string& virtualPath, const std::string& data);
// Entries sorted by name, without "." and "..". False if the folder cannot be opened.
bool listDir(const std::string& virtualPath, std::vector<DirEntry>& out);
// Deletes a file, or a folder and everything in it. True if nothing is left afterwards (also if it never existed).
bool removeAll(const std::string& virtualPath);
// Renames a file or folder, replacing a file at the destination (which survives if the rename fails). Virtual paths.
bool rename(const std::string& fromVirtual, const std::string& toVirtual);
// Size and modification time (unix seconds) of a file; false if it does not exist.
bool fileInfo(const std::string& virtualPath, uint64_t& size, int64_t& mtime);

// Sequential writer (large files): open, write chunks, close. Parent folders are created.
// A write the card refuses (EIO, seen on the device while Wi-Fi received at the same time: Files over Wi-Fi) is tried again
// up to three times: the file is reopened and the piece written again at the same place.
class Writer {
 public:
  ~Writer() { close(); }
  bool open(const std::string& virtualPath);
  bool write(const void* data, size_t n);
  bool close();  // true if everything reached the file
  unsigned retries() const { return retries_; }  // writes that needed another try

 private:
  FILE* f_ = nullptr;
  bool ok_ = true;
  std::string path_;  // the real path, for a reopen
  uint64_t pos_ = 0;  // bytes written so far
  unsigned retries_ = 0;
};

// Random-access reader (large files read in pieces, e.g. the emoji atlases): open once, read ranges, close.
class Reader {
 public:
  Reader() = default;
  Reader(const Reader&) = delete;
  Reader& operator=(const Reader&) = delete;
  ~Reader() { close(); }
  bool open(const std::string& virtualPath);
  // True if all `n` bytes at `offset` were read.
  bool readAt(uint64_t offset, void* data, size_t n);
  void close();

 private:
  FILE* f_ = nullptr;
};

// Bumped by every writeFile, rename and removeAll on the card (a file replaced or gone): a cache of a file's contents compares it cheaply and checks
// the file again only when it moved (ui/emoji: atlases replaced over USB while the device runs).
unsigned changes();

// Creates the folder and every missing parent ("mkdir -p"). True if it exists afterwards.
bool mkdirs(const std::string& virtualPath);

}  // namespace pos::fs
