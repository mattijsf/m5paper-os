// System settings: /data/config.json (no secrets). docs/apps/settings.md has the schema. Every key is
// optional; a missing or unreadable file, or a value of the wrong type or range, gives the default.
#pragma once

#include <cstdint>
#include <string>

namespace pos {

struct DisplayConfig {
  float ghostLimit = 8.0f;   // 4..16
  int settleMs = 600;        // 100..3000
  int idleCleanMs = 2000;    // 500..30000
  int pageCleanEvery = 10;   // 0..100, 0 = off: a Quality pass every N full-page changes (page turns)
};
// What the device does when nobody touched it for PowerConfig::autoOffMin (DECISIONS.md D38). On battery it switches off
// either way, the sleep screen on the panel; this is what it shows while it stays on (on USB, or USB seen in the last
// 12 hours: power_policy.h kUsbGraceMs).
enum class IdleShow : uint8_t {
  Home,         // the open app is left for the launcher (not a book or a game: AppInfo::stayOpen)
  SleepScreen,  // the sleep screen (the Today board); a tap brings back the launcher, or the book or game that was open
};

struct PowerConfig {
  // Minutes without a touch until the device switches off (on battery) or shows what `idleShow` says (on USB).
  int autoOffMin = 15;        // 0 = never
  int autoOffMinReader = 30;  // the same while the reader is open
  IdleShow idleShow = IdleShow::SleepScreen;
  // Do not light-sleep while a USB host is connected: sleep breaks the serial link (tools/device.py). Auto-off
  // never happens on USB either.
  bool stayAwakeOnUsb = true;
  // One line every 10 minutes in /data/power.csv (battery voltage, state, Wi-Fi time, refreshes): the data of the battery runs (POWER.md "Measuring").
  bool log = true;
  // Scheduled refresh (P7-5): while switched off, wake by the RTC every this many hours, read the relay (inbox, agenda) and the weather, redraw the sleep
  // screen and switch off again. 0 = off (default): the device then stays off until the side button.
  int refreshHours = 0;
  // The Today board on the sleep screen (P7-16): the date, the weather and the agenda's next events instead of the cover or
  // the mark alone. False: the plain screen (one agenda line, the unread count).
  bool sleepBoard = true;
};
struct NetConfig {
  int lingerSec = 20;
  std::string timezone = "CET-1CEST,M3.5.0,M10.5.0/3";  // POSIX TZ string
};
struct ReaderConfig {
  std::string fontSize = "M";     // S, M, L, XL
  std::string margins = "normal"; // narrow, normal, wide
  float lineSpacing = 1.35f;      // 1.1..1.8
};
struct HaConfig {
  std::string baseUrl = "http://homeassistant.local:8123";
};
struct LauncherConfig {
  bool resumeLastApp = true;
};
struct PhoneConfig {
  bool shareClipboard = true;  // the clipboard goes to and comes from the paired phone (DECISIONS.md D23)
  bool sound = false;          // a chime when a read of the relay brings new messages (core/sound.h)
};

class Config {
 public:
  explicit Config(std::string path = "/data/config.json") : path_(std::move(path)) {}

  DisplayConfig display;
  PowerConfig power;
  NetConfig net;
  ReaderConfig reader;
  HaConfig ha;
  LauncherConfig launcher;
  PhoneConfig phone;

  // Reads the file; anything missing or invalid keeps its default. Returns true if a file was read.
  bool load();
  // Writes the whole config atomically. False if the write failed.
  bool save() const;
  // Defaults for everything.
  void reset();

 private:
  std::string path_;
};

}  // namespace pos
