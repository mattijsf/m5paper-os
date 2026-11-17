#include "core/sound.h"

#include <atomic>

#include "core/log.h"
#include "core/timefmt.h"
#include "hal/system.h"

namespace pos::sound {

namespace {
std::atomic<bool> gNewMessages{false};
std::atomic<int> gChimes{0};
}  // namespace

void setNewMessages(bool on) { gNewMessages = on; }
bool newMessages() { return gNewMessages; }

bool quiet(int64_t now) {
  struct tm t;
  if (!clockIsSet(now) || !localTime(now, t)) return false;
  return t.tm_hour >= kQuietFromHour || t.tm_hour < kQuietToHour;
}

bool arrived(int fresh, int64_t now) {
  if (fresh <= 0 || !gNewMessages) return false;
  if (quiet(now)) {
    POS_LOG("sound", "%d new message(s): quiet, it is night (%s)", fresh, clockText(now).c_str());
    return false;
  }
  POS_LOG("sound", "%d new message(s)", fresh);
  play();
  return true;
}

void play() {
  ++gChimes;
  hal::chime();
}

int chimes() { return gChimes; }

}  // namespace pos::sound
