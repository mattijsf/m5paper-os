// The power log (docs/POWER.md "Measuring", task P3-5): one CSV line every 10 minutes in /data/power.csv, so a battery run can be read
// afterwards from the voltage curve. Switched by config power.log (default on; a line is 50 bytes, 144 a day).
//   unix,mv,state,wifiSeconds,pushes,usb,app
//   state: "active" (touched in the last minute) or "idle"; wifiSeconds: seconds the Wi-Fi was on since boot; pushes: panel
//   refreshes since boot (all modes); usb: 1 if a host or charger was seen; app: id of the open app, "" = launcher.
#pragma once

#include <cstdint>
#include <string>

namespace pos {

class Os;

constexpr uint32_t kPowerLogIntervalMs = 10 * 60 * 1000;
constexpr uint64_t kPowerLogMaxBytes = 200 * 1024;  // then the file becomes power.old.csv and a new one starts

class PowerLog {
 public:
  explicit PowerLog(Os& os) : os_(os) {}
  // Every loop pass: accumulates the Wi-Fi time, writes a line when one is due (the first at the first pass after boot).
  void tick();
  // Milliseconds until a line is due (the loop must wake for it), kNoDeadline if the log is off.
  uint32_t msToNext();
  uint32_t wifiSeconds() const { return static_cast<uint32_t>(wifiMs_ / 1000); }
  int lines() const { return lines_; }
  const std::string& lastLine() const { return last_; }

  static const char* header();
  static std::string formatLine(int64_t unix, int mv, bool active, uint32_t wifiSeconds, uint32_t pushes, bool usb, const std::string& app);

 private:
  void write();
  Os& os_;
  uint32_t lastTickMs_ = 0, nextMs_ = 0;
  bool started_ = false, wasOnline_ = false;
  uint64_t wifiMs_ = 0;
  int lines_ = 0;
  std::string last_;
};

}  // namespace pos
