#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "core/log.h"
#include "platform/device/device.h"

namespace pos::device {

StorageInfo mountStorage() {
  StorageInfo s;

  // microSD: pins from docs/HARDWARE.md. Mounted at /sd through the ESP-IDF VFS, so POSIX works.
  SPI.begin(39, 40, 38, 47);
  s.sdOk = SD.begin(47, SPI, 25000000, "/sd");
  if (s.sdOk) {
    s.sdTotalMb = static_cast<uint32_t>(SD.totalBytes() / (1024 * 1024));
    s.sdFreeMb = static_cast<uint32_t>((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024));
    POS_LOG("storage", "sd mounted at /sd: type=%d size=%uMB free=%uMB", static_cast<int>(SD.cardType()),
            static_cast<unsigned>(s.sdTotalMb), static_cast<unsigned>(s.sdFreeMb));
  } else {
    POS_LOG("storage", "no SD card");
  }

  // Internal flash: the partition named "storage" (partitions.csv), formatted if it has no file system.
  s.dataOk = LittleFS.begin(true, "/littlefs", 10, "storage");
  if (s.dataOk) {
    s.dataTotalKb = static_cast<uint32_t>(LittleFS.totalBytes() / 1024);
    s.dataFreeKb = static_cast<uint32_t>((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024);
    POS_LOG("storage", "littlefs mounted at /littlefs: size=%uKB free=%uKB", static_cast<unsigned>(s.dataTotalKb),
            static_cast<unsigned>(s.dataFreeKb));
  } else {
    POS_LOG("storage", "littlefs mount FAILED");
  }
  return s;
}

}  // namespace pos::device
