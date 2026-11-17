// HAL hooks for system facts that are not worth a class: storage sizes, memory, restart, identity.
// Implemented by each platform (device: SD/LittleFS libraries and ESP-IDF; simulator: fixed fake values so
// screenshots are deterministic).
#pragma once

#include <cstdint>
#include <string>

namespace pos::hal {

struct StorageInfo {
  bool present = false;
  uint64_t totalBytes = 0, freeBytes = 0;
};
// which: "sd" (microSD) or "data" (internal flash).
StorageInfo storageInfo(const char* which);

struct MemoryInfo {
  uint32_t heapFree = 0, heapTotal = 0, psramFree = 0, psramTotal = 0;
};
MemoryInfo memoryInfo();

// Restarts the firmware (the simulator exits).
void restart();
// Board name and the Wi-Fi MAC address ("" if unknown).
std::string boardName();
std::string macAddress();

// The one sound: two short notes for "something new" (device: the buzzer; the simulator only logs it). Does not block and
// may be called from any thread; a call while it plays is dropped. Use core/sound.h, which holds the setting.
void chime();

}  // namespace pos::hal
