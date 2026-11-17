// Files over Wi-Fi (docs/apps/settings.md "Files over Wi-Fi", DECISIONS.md D30): the answers of the Paper's file server, as
// WebDAV (Finder: Go > Connect to Server; Android file managers) and as a small upload page for a browser. Pure logic over
// core/fs and the request / response of hal/http_server: unit-tested. The server runs only while its Settings page is open.
//
// What it allows: everything inside `root` (the books folder), nothing outside it; names that start with a dot stay hidden
// and cannot be written, except that the Mac's "._name" and ".DS_Store" files are accepted and thrown away (Finder would stop
// a copy if they failed). Every request but OPTIONS needs the one-time code as the HTTP password (any name), by Digest (macOS
// sends no Basic password over plain HTTP: Finder kept asking) or Basic (any browser); ten wrong codes lock it for a minute.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "hal/http_server.h"

namespace pos::files {

struct Stats {
  unsigned received = 0;   // files written
  uint64_t bytes = 0;      // their bytes
  unsigned deleted = 0;
  std::string last;        // "Received alice.epub" (the latest change; empty before)
  unsigned refused = 0;    // wrong codes
  bool busy = false;       // a file is being received now
  std::string receiving;   // its name
  uint64_t receivingBytes = 0;
};

class FileServer {
 public:
  // `root`: a virtual folder ("/sd/books"); `code`: the 6 digits; `millis`: a clock for the lockout and idle times.
  FileServer(std::string root, std::string code, std::function<uint32_t()> millis);

  // Thread-safe (the server's connection threads).
  hal::ServerResponse handle(hal::ServerRequest& req);
  Stats stats() const;
  uint32_t lastRequestMs() const;  // millis() of the last authorised request (or the start)

  static constexpr unsigned kMaxRefusals = 10;
  static constexpr uint32_t kLockoutMs = 60 * 1000;

 private:
  hal::ServerResponse route(hal::ServerRequest& req, const std::string& path, const std::string& query);
  bool authorised(const hal::ServerRequest& req);
  hal::ServerResponse propfind(hal::ServerRequest& req, const std::string& path);
  hal::ServerResponse put(hal::ServerRequest& req, const std::string& path);
  hal::ServerResponse moveOrCopy(hal::ServerRequest& req, const std::string& path, bool move);
  hal::ServerResponse lock(hal::ServerRequest& req, const std::string& path);
  // The virtual path (core/fs) of a request path: inside the served folder.
  std::string real(const std::string& path) const { return root_ + path; }
  // A hidden name beside `path` to write to before the rename; never the same twice in one run.
  std::string tempBeside(const std::string& path, const char* prefix);

  static constexpr const char* kRealm = "PaperOS";
  const std::string root_, code_;
  std::string nonce_;  // the Digest nonce of this run (random)
  std::function<uint32_t()> millis_;
  mutable std::mutex mu_;
  Stats stats_;
  uint32_t lastRequestMs_ = 0;
  uint32_t lockedUntilMs_ = 0;
  unsigned tokenCounter_ = 0;
  std::atomic<unsigned> tempCounter_{0};
};

// Six random digits.
std::string newCode();

// Helpers, exposed for the unit tests.
std::string percentDecode(const std::string& s, bool& ok);  // "%20" -> " "; ok false for a broken escape or a NUL
std::string percentEncodePath(const std::string& path);     // each segment encoded, the slashes kept
std::string base64Decode(const std::string& s, bool& ok);
std::string httpDate(int64_t unixSeconds);                  // "Mon, 16 Nov 2026 20:12:00 GMT"
// A request path made safe: decoded, "/" for the root, no "..", "." or empty segments, a trailing slash dropped. "" if refused.
std::string cleanPath(const std::string& rawPath);
bool isHidden(const std::string& path);      // a segment starts with "."
bool isMacJunk(const std::string& path);     // the last segment is "._*" or ".DS_Store"

}  // namespace pos::files
