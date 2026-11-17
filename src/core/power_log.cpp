#include "core/power_log.h"

#include "core/display_manager.h"
#include "core/fs.h"
#include "core/os.h"

namespace pos {

const char* PowerLog::header() { return "unix,mv,state,wifiSeconds,pushes,usb,app\n"; }

std::string PowerLog::formatLine(int64_t unix, int mv, bool active, uint32_t wifiSeconds, uint32_t pushes, bool usb, const std::string& app) {
  return std::to_string(unix) + "," + std::to_string(mv) + "," + (active ? "active" : "idle") + "," + std::to_string(wifiSeconds) + "," +
         std::to_string(pushes) + "," + (usb ? "1" : "0") + "," + app + "\n";
}

void PowerLog::tick() {
  hal::Platform& p = os_.platform();
  const uint32_t now = p.clock.millis();
  if (started_ && wasOnline_) wifiMs_ += now - lastTickMs_;
  lastTickMs_ = now;
  wasOnline_ = os_.net().online();
  if (!os_.config().power.log) return;
  if (!started_ || static_cast<int32_t>(now - nextMs_) >= 0) {
    started_ = true;
    nextMs_ = now + kPowerLogIntervalMs;
    write();
  }
  started_ = true;
}

uint32_t PowerLog::msToNext() {
  if (!os_.config().power.log || !started_) return os_.config().power.log ? 0 : kNoDeadline;
  const int32_t d = static_cast<int32_t>(nextMs_ - os_.platform().clock.millis());
  return d <= 0 ? 0 : static_cast<uint32_t>(d);
}

void PowerLog::write() {
  hal::Platform& p = os_.platform();
  const char* path = "/data/power.csv";
  uint64_t size = 0;
  int64_t mtime = 0;
  const bool exists = fs::fileInfo(path, size, mtime);
  // A full file becomes power.old.csv and a new one starts. If that fails the old file goes on (no second header).
  const bool rotated = exists && size > kPowerLogMaxBytes && fs::rename(path, "/data/power.old.csv");
  std::string text = (!exists || rotated) ? header() : "";
  const DisplayManager::Counts& c = os_.displayManager().counts();
  const bool active = p.clock.millis() - os_.lastInputMs() < 60 * 1000 && os_.lastInputMs() != 0;
  Surface* s = os_.surface();
  last_ = formatLine(p.clock.now(), p.power.batteryMv(), active, wifiSeconds(), c.quality + c.text + c.fast,
                     p.power.usbConnected() || p.power.charging(), s ? s->currentAppId() : "");
  text += last_;
  if (fs::appendFile(path, text)) ++lines_;
}

}  // namespace pos
