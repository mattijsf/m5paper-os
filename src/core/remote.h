// Remote control: text commands that inject touch and dump the frame, so device checks (and
// simulator runs) need neither eyes nor fingers. Specified in docs/ROADMAP.md P1-8.
//
// Transport: lines starting with '>' (device: USB serial; simulator: stdin with --remote).
//   >shot                       #shot 540 960 <bytes> <crc32 hex>\n + raw 4-bit pixels, then #ok
//   >tap x y   >long x y   >swipe x1 y1 x2 y2    inject touch events as a real touch would
//   >log                        #log <bytes>\n + the last 50 log lines, then #ok
//   >refresh                    #refresh <bytes>\n + the last 50 refresh-log lines, then #ok
//   >set key value              platform debug switch, e.g. ">set epd-timing on" (device)
//   >reboot                     #ok, then restart
// Every command ends with "#ok" or "#err <reason>" on its own line.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
  enum Kind { None, Shot, Tap, Long, Swipe, Log, Refresh, Reboot, Set } kind = None;
  std::string key, value;  // Set
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  std::string error;  // set when parsing fails (kind stays None)
};

// Pure parser: "tap 10 20" (with or without the leading '>') -> command. No side effects.
RemoteCommand parseRemoteCommand(const std::string& line);

uint32_t crc32(const uint8_t* data, size_t len);

class Remote {
 public:
  Remote(Os& os, hal::Clock& clock, RemoteHost& host) : os_(os), clock_(clock), host_(host) {}

  // Handles one received line. Lines that do not start with '>' are ignored.
  void handleLine(const std::string& line);
  // Call once per main-loop pass, after Os::loopOnce(): releases timed touch events and runs a
  // pending >shot once the injected touches have been processed.
  void tick();
  bool busy() const { return !timed_.empty() || shotPending_; }

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
  bool shotPending_ = false;
  int settle_ = 0;
};

}  // namespace pos
