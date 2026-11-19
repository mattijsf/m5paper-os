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
//   >ota <size>                 firmware update (D37, D39): replies "#ota go", then the host sends the signed package
//                               (tools/ota.py; "#ack <total>" after every 4096 bytes, as >put), then "#ok" and a restart
//                               into it, or "#err <why>" (not signed with this device's key, the image changed, ...).
// Every command ends with "#ok" or "#err <reason>" on its own line.
// A session of the paired phone (core/remote_phone.h, signed in with ">auth-phone") may only send >shot, >refresh, >tap,
// >long, >swipe and >ota (still signed with the update key); anything else gets "#err not from the phone".
//
// Transports: the USB serial port; the simulator's stdin; and Wi-Fi while Settings > Developer > Wireless development is on
// (platform/posix/remote_server, port 8266, a signed handshake first: core/update.h helloLine / authOk).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/crc32.h"
#include "core/fs.h"
#include "core/os.h"
#include "core/update.h"
#include "hal/clock.h"
#include "hal/input.h"

namespace pos {

// What the platform provides to the remote.
class RemoteHost {
 public:
  virtual ~RemoteHost() = default;
  virtual void inject(const hal::TouchEvent& ev) = 0;  // into the Input queue
  virtual void reply(const void* data, size_t n) = 0;  // raw bytes back to the controller
  // Bytes reply() can take right now without waiting (the device: free space in the USB TX buffer). Replies are queued and
  // sent in pieces of at most this from tick(), so a 259 KB frame dump no longer holds the UI loop for a second.
  virtual size_t writable() { return static_cast<size_t>(-1); }
  // true: a reply is on its way out and may leave in pieces over several loop passes; false: it is out. The device holds
  // its log lines back meanwhile: a line printed between two pieces lands inside the reply (a heartbeat in the pixels of a
  // frame dump: the host then never sees "#ok" and reports "no reply to >shot", 2026-11-16).
  virtual void replyLeaving(bool /*on*/) {}
  virtual void reboot() = 0;
  // Platform-specific debug switch (">set key value"). Returns false for unknown keys.
  virtual bool option(const std::string& /*key*/, const std::string& /*value*/) { return false; }
};

struct RemoteCommand {
  enum Kind { None, Shot, Tap, Long, Swipe, Log, Refresh, Reboot, Set, Status, Put, Ls, Rm, PowerOff, Cat, State, Ota } kind = None;
  std::string key, value;  // Set
  std::string path;        // Put, Ls, Rm
  uint64_t size = 0;       // Put, Ota
  uint32_t crc = 0;        // Put
  // PowerOff: seconds in x1 (1..3600), y1 = seconds of a second power-off after the scheduled refresh (0 = none). 0 (power off until the side button is pressed) is refused: nobody can press it remotely.
  int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
  std::string error;  // set when parsing fails (kind stays None)
};

// Pure parser: "tap 10 20" (with or without the leading '>') -> command. No side effects.
RemoteCommand parseRemoteCommand(const std::string& line);

// Paths a remote file command may touch: /sd/books/... and /sd/paperos/..., no "..", never paperos_config.
bool remotePathAllowed(const std::string& virtualPath);

// What Settings > Developer shows about wireless development (UI thread; written by the platform's server and the remote).
struct WirelessStatus {
  bool on = false;         // the setting
  bool listening = false;  // on the Wi-Fi, port `port`
  bool client = false;     // a Mac or the paired phone is connected (signed in)
  bool phone = false;      // the client is the phone (core/remote_phone.h: screen and touch only)
  std::string address;     // "192.168.1.60"
  uint16_t port = 0;
  std::string activity;    // "Receiving build 3fa9c1d2e4b7: 1.2 MB of 2.5 MB", "" for none
};
WirelessStatus& wirelessStatus();

class Remote {
 public:
  // `phone`: a session of the paired phone (core/remote_phone.h): only the screen and touch commands.
  Remote(Os& os, hal::Clock& clock, RemoteHost& host, bool phone = false) : os_(os), clock_(clock), host_(host), phone_(phone) {}

  // What the transport received (serial port, the simulator's stdin), in any chunking: command lines, and during an
  // upload the file's bytes. A line longer than kMaxLine is not run: cut short it would be another command
  // (">rm /sd/books/long name" as ">rm /sd/books/lo"); it is answered "#err line too long".
  void feed(const uint8_t* data, size_t n);
  static constexpr size_t kMaxLine = 512;
  // Handles one received line. Lines that do not start with '>' are ignored.
  void handleLine(const std::string& line);
  // Call once per main-loop pass, after Os::loopOnce(): releases timed touch events and runs a
  // pending >shot once the injected touches have been processed.
  void tick();
  bool busy() const { return !timed_.empty() || shotPending_ || put_.active || ota_.active || outPos_ < out_.size(); }
  // True while an upload is running: feed() takes the incoming bytes as file content.
  bool wantsRaw() const { return put_.active || ota_.active; }
  // The file bytes of the running upload; returns how many it took (the rest, after the last byte, is a line again).
  size_t feedRaw(const uint8_t* data, size_t n);

 private:
  struct Timed {
    uint32_t at;
    hal::TouchEvent ev;
  };
  void say(const std::string& s);
  void send(const void* data, size_t n, bool closes = false);  // queue reply bytes (sent by pump()); closes: the reply's last line
  void pump();                            // send what the host takes now
  void flushAll();                        // send everything, waiting (before a reboot or power-off)
  void setLeaving(bool on);               // tells the host once per change
  bool leaving_ = false;
  bool replyOpen_ = false;                // a reply's closing "#ok" / "#err" line has not been queued yet
  std::vector<uint8_t> out_;
  size_t outPos_ = 0;
  void queue(uint32_t at, hal::TouchEvent::Type t, int x, int y);
  void doShot();

  Os& os_;
  hal::Clock& clock_;
  RemoteHost& host_;
  bool phone_ = false;
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
  void finishPut();
  struct Ota {
    bool active = false;
    uint64_t size = 0, got = 0;
    uint64_t discard = 0;  // bytes still coming after a refusal: dropped
    uint32_t lastByteMs = 0;
    std::unique_ptr<update::ImageWriter> writer;
  } ota_;
  size_t feedOta(const uint8_t* data, size_t n);
  void endOta(const std::string& why);  // "" = the image is in place
  std::string line_;       // the line being received (feed)
  bool lineTooLong_ = false;
  bool shotPending_ = false;
  int settle_ = 0;
};

}  // namespace pos
