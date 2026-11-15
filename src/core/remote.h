// Remote control: text commands that inject touch and dump the frame, so device checks (and
// simulator runs) need neither eyes nor fingers. Specified in docs/ROADMAP.md P1-8.
//
// Transport: lines starting with '>' (device: USB serial; simulator: stdin with --remote).
//   >shot                       #shot 540 960 <bytes> <crc32 hex>\n + raw 4-bit pixels, then #ok
//   >tap x y   >long x y   >swipe x1 y1 x2 y2    inject touch events as a real touch would
//   >log                        #log <bytes>\n + the last 50 log lines, then #ok
//   >refresh                    #refresh <bytes>\n + the last 50 refresh-log lines, then #ok
//   >status                     #status <bytes>\n + key names and lengths (never values), Wi-Fi, memory; then #ok
//   >set key value              platform debug switch: ">set epd-timing on" (device), ">set screen widgets2"
//                               (a UI test screen, an app id, or "launcher")
//   >put <path> <size> <crc32 hex>   upload: replies "#put go", then the host sends raw bytes (any chunking; the device
//                               answers "#ack <total>" after every 4096 bytes), then "#ok" (crc matched, file renamed
//                               from <path>.part) or "#err ...". Only under /sd/books/ and /sd/paperos/, never
//                               paperos_config. The path may contain spaces.
//   >ls <dir>                   "#ls <bytes>\n" + "name<TAB>size" lines (folders: size -), then #ok. Same path limits.
//   >rm <path>                  deletes a file or folder. Same path limits.
//   >reboot                     #ok, then restart
// Every command ends with "#ok" or "#err <reason>" on its own line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/fs.h"
#include "core/os.h"
#include "hal/clock.h"
#include "hal/input.h"

namespace pos {

// What the platform provides to the remote.
class RemoteHost {
 public:
  virtual ~RemoteHost() = default;
  virtual void inject(const hal::TouchEvent& ev) = 0;  // into the Input queue
  virtual void reply(const void* data, size_t n) = 0;  // raw bytes back to the controller
  virtual void reboot() = 0;
  // Platform-specific debug switch (">set key value"). Returns false for unknown keys.
  virtual bool option(const std::string& /*key*/, const std::string& /*value*/) { return false; }
};

struct RemoteCommand {
  enum Kind { None, Shot, Tap, Long, Swipe, Log, Refresh, Reboot, Set, Status, Put, Ls, Rm, PowerOff, Cat, State } kind = None;
  std::string key, value;  // Set
  std::string path;        // Put, Ls, Rm
  uint64_t size = 0;       // Put
  uint32_t crc = 0;        // Put
  // PowerOff: seconds in x1 (1..3600), y1 = seconds of a second power-off after the scheduled refresh (0 = none). 0 (power off until the side button is pressed) is refused: nobody can press it remotely.
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  std::string error;  // set when parsing fails (kind stays None)
};

// Pure parser: "tap 10 20" (with or without the leading '>') -> command. No side effects.
RemoteCommand parseRemoteCommand(const std::string& line);

uint32_t crc32(const uint8_t* data, size_t len);
// Continues a CRC-32: crc32Update(crc32Update(0, a), b) == crc32(a + b).
uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len);
// Paths a remote file command may touch: /sd/books/... and /sd/paperos/..., no "..", never paperos_config.
bool remotePathAllowed(const std::string& virtualPath);

class Remote {
 public:
  Remote(Os& os, hal::Clock& clock, RemoteHost& host) : os_(os), clock_(clock), host_(host) {}

  // Handles one received line. Lines that do not start with '>' are ignored.
  void handleLine(const std::string& line);
  // Call once per main-loop pass, after Os::loopOnce(): releases timed touch events and runs a
  // pending >shot once the injected touches have been processed.
  void tick();
  bool busy() const { return !timed_.empty() || shotPending_ || put_.active; }
  // True while an upload is running: the transport must pass incoming bytes to feedRaw(), not to handleLine().
  bool wantsRaw() const { return put_.active; }
  void feedRaw(const uint8_t* data, size_t n);

 private:
  struct Timed {
    uint32_t at;
    hal::TouchEvent ev;
  };
  void say(const std::string& s);
  void queue(uint32_t at, hal::TouchEvent::Type t, int x, int y);
  void doShot();

  Os& os_;
  hal::Clock& clock_;
  RemoteHost& host_;
  std::vector<Timed> timed_;
  struct Put {
    bool active = false;
    std::string path, partPath;
    uint64_t size = 0, got = 0;
    uint32_t crc = 0, expect = 0;
    fs::Writer file;
    uint32_t lastByteMs = 0;
  } put_;
  void abortPut(const std::string& why);
  bool shotPending_ = false;
  int settle_ = 0;
};

}  // namespace pos
