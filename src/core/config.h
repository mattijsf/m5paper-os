// System settings: /data/config.json (no secrets). docs/apps/settings.md has the schema. Every key is
// optional; a missing or unreadable file, or a value of the wrong type or range, gives the default.
#pragma once

#include <string>
#include <vector>

namespace pos {

struct DisplayConfig {
  float ghostLimit = 8.0f;   // 4..16
  int settleMs = 600;        // 100..3000
  int idleCleanMs = 2000;    // 500..30000
};
struct PowerConfig {
  int autoOffMin = 15;        // 0 = never (on USB)
  int autoOffMinReader = 30;
  // Do not light-sleep while a USB host is connected: sleep breaks the serial link (tools/device.py). Auto-off
  // never happens on USB either.
  bool stayAwakeOnUsb = true;
  // One line every 10 minutes in /data/power.csv (battery voltage, state, Wi-Fi time, refreshes): the data of the battery runs (POWER.md "Measuring").
  bool log = true;
  // Scheduled refresh (P7-5): while switched off, wake by the RTC every this many hours, fetch the inbox feeds, redraw the sleep screen
  // with the new summary and switch off again. 0 = off (default): the device then stays off until the side button.
  int refreshHours = 0;
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
// A WhatsApp chat the device may start a message to ("New message", a game's "Send score"): DECISIONS.md D21.
struct ChatFavorite {
  std::string name;  // shown on the device, at most 40 characters
  std::string chat;  // the chat JID: <digits>@s.whatsapp.net or <digits>@g.us (checked by the WhatsApp app, not here)
};
struct InboxConfig {
  std::vector<ChatFavorite> whatsappFavorites;  // inbox.whatsapp.favorites, at most 8 (one screen); entries without a name or chat are skipped
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
  InboxConfig inbox;

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
