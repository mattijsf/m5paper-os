// Device entry point. Builds the PaperS3 Platform and hands it to pos::Os.
// Display, input, clock and storage live in display/input/clock/storage.cpp; Net and Power are
// still stubs here (P3). Also keeps the boot report, SD config-key check and serial heartbeat.

#include <Arduino.h>
#include <M5Unified.h>
#include <LittleFS.h>
#include <SD.h>
#include <esp_mac.h>
#include <esp_pthread.h>

#include <algorithm>
#include <string>

#include "core/fs.h"
#include "core/gfx.h"
#include "core/log.h"
#include "apps/apps.h"
#include "core/loop.h"
#include "core/os.h"
#include "core/remote.h"
#include "hal/platform.h"
#include "hal/system.h"
#include "platform/device/device.h"

using namespace pos;

namespace pos::hal {
void logWrite(const char* line) { Serial.print(line); }

StorageInfo storageInfo(const char* which) {
  StorageInfo s;
  if (std::string(which) == "sd") {
    if (SD.cardType() == CARD_NONE) return s;
    s.present = true;
    s.totalBytes = SD.totalBytes();
    s.freeBytes = s.totalBytes - SD.usedBytes();
  } else {
    s.present = true;
    s.totalBytes = LittleFS.totalBytes();
    s.freeBytes = s.totalBytes - LittleFS.usedBytes();
  }
  return s;
}
MemoryInfo memoryInfo() {
  return MemoryInfo{ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize()};
}
void restart() { esp_restart(); }
std::string boardName() { return M5.getBoard() == m5::board_t::board_M5PaperS3 ? "M5PaperS3" : "unknown board"; }
std::string macAddress() {
  uint8_t m[6];
  if (esp_efuse_mac_get_default(m) != ESP_OK) return "";
  char buf[20];
  snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return buf;
}
}  // namespace pos::hal

namespace {

device::Display gDisplay;
device::Input gInput;
device::Clock gClock;
device::Net gNet;
device::Power gPower;
hal::Platform gPlatform{gDisplay, gInput, gClock, gNet, gPower, "M5PaperS3"};
Os gOs(gPlatform);

// Idle step: PowerPolicy light-sleeps when everything allows it (core/power_policy.h), else a short delay.
PowerPolicy gPolicy(gOs, [](uint32_t ms) {
  if (ms > 0) delay(ms < 10 ? ms : 10);
});
Loop gLoop(gOs, gPolicy);

// Remote control over the USB serial port (lines starting with '>', see core/remote.h).
class SerialRemoteHost : public RemoteHost {
 public:
  void inject(const hal::TouchEvent& ev) override { gInput.inject(ev); }
  void reply(const void* data, size_t n) override {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    int stalls = 0;
    while (n > 0 && stalls < 500) {  // the host reads while it waits; give up after ~0.5 s of no progress
      size_t w = Serial.write(p, n > 512 ? 512 : n);
      if (w == 0) {
        delay(1);
        ++stalls;
        continue;
      }
      stalls = 0;
      p += w;
      n -= w;
    }
  }
  bool option(const std::string& key, const std::string& value) override {
    if (key == "screen") {  // ">set screen widgets2": a UI test screen, an app id, or "launcher"
      auto s = apps::createSurface(value == "launcher" ? "" : value, gOs);
      if (!s) return false;
      gOs.setSurface(std::move(s));
      return true;
    }
    // Power tests: ">set sleep 5000" light-sleeps 5 s (the result is logged), ">set poweroff 20" powers off with
    // an RTC alarm after 20 s (">set poweroff 0": only the side button brings it back).
    if (key == "sleep") {
      gPower.lightSleep(static_cast<uint32_t>(atoi(value.c_str())));
      return true;
    }
    if (key == "usbsleep") {
      // Test aid: allow light sleep on USB for N seconds, then switch it off again by itself. While it sleeps the
      // device cannot be reached; read the log after the window ends (">set usbsleep 30", wait, ">log").
      const int sec = atoi(value.c_str());
      gOs.config().power.stayAwakeOnUsb = false;
      gOs.timers().after(static_cast<uint32_t>(sec) * 1000u, [] { gOs.config().power.stayAwakeOnUsb = true; });
      return true;
    }
    if (key == "poweroff") {
      gPower.powerOff(static_cast<uint32_t>(atoi(value.c_str())));
      return true;
    }
    if (key == "epd-timing") {
      gDisplay.setTiming(value == "on");
      return true;
    }
    return false;
  }
  void reboot() override {
    Serial.flush();
    delay(100);
    esp_restart();
  }
};
SerialRemoteHost gRemoteHost;
Remote gRemote(gOs, gClock, gRemoteHost);
std::string gRxLine;

void pollSerial() {
  while (Serial.available() > 0) {
    if (gRemote.wantsRaw()) {  // an upload (>put): the bytes are file content
      static uint8_t raw[4096];
      const size_t n = Serial.readBytes(reinterpret_cast<char*>(raw), std::min<size_t>(sizeof raw, Serial.available()));
      if (n == 0) break;
      gRemote.feedRaw(raw, n);
      continue;
    }
    int ch = Serial.read();
    if (ch < 0) break;
    if (ch == '\n') {
      gRemote.handleLine(gRxLine);
      gRxLine.clear();
    } else if (gRxLine.size() < 128) {
      gRxLine += static_cast<char>(ch);
    }
  }
}

device::StorageInfo gStorage;
std::string gCfgKeys = "(not read)";

// Provisioning file check: report key names and value lengths only, never values.
std::string configKeyReport() {
  if (!gStorage.sdOk) return "(no SD card)";
  std::string text;
  if (!pos::fs::readFile("/sd/paperos_config", text)) return "(file missing)";
  std::string out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    size_t eq = line.find('=');
    if (line.empty() || line[0] == '#' || eq == std::string::npos || eq == 0) continue;
    out += line.substr(0, eq) + "(" + std::to_string(line.size() - eq - 1) + ") ";
  }
  return out.empty() ? "(no keys)" : out;
}

// Storage check: /data/test.txt is created on the first boot and must be found unchanged after.
void dataFileCheck() {
  if (!gStorage.dataOk) return;
  std::string content;
  if (pos::fs::readFile("/data/test.txt", content)) {
    POS_LOG("fs", "/data/test.txt found after reboot: %u bytes: %s", (unsigned)content.size(), content.c_str());
  } else {
    char buf[48];
    snprintf(buf, sizeof buf, "P1-2 test %08x", (unsigned)esp_random());
    bool ok = pos::fs::writeFile("/data/test.txt", buf);
    POS_LOG("fs", "/data/test.txt created (write %s): %s", ok ? "ok" : "FAILED", buf);
  }
  // Boot counter: rewritten every boot, so it also exercises the rename-over-existing path.
  std::string n;
  int count = pos::fs::readFile("/data/boot_count", n) ? atoi(n.c_str()) : 0;
  bool ok = pos::fs::writeFile("/data/boot_count", std::to_string(count + 1));
  POS_LOG("fs", "boot count %d (write %s)", count + 1, ok ? "ok" : "FAILED");
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.setRxBufferSize(16384);  // uploads arrive in 4 KB bursts
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block the UI thread on a USB host that is not reading the log

  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);

  POS_LOG("boot", "board=%d display=%dx%d rot=%d", (int)M5.getBoard(), (int)M5.Display.width(),
          (int)M5.Display.height(), (int)M5.Display.getRotation());
  POS_LOG("boot", "psram=%u free=%u flash=%u heap=%u", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram(),
          (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getFreeHeap());
  POS_LOG("boot", "battery=%dmV level=%d%% charging=%d", (int)M5.Power.getBatteryVoltage(),
          (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());

  POS_LOG("boot", "clock now=%lld (0 = RTC never set)", (long long)gClock.now());
  gStorage = device::mountStorage();
  gCfgKeys = configKeyReport();
  POS_LOG("boot", "config keys(len): %s", gCfgKeys.c_str());
  dataFileCheck();

  // The worker thread gets a 16 KB stack (docs/ARCHITECTURE.md): set before Os::setup() starts it.
  esp_pthread_cfg_t pcfg = esp_pthread_get_default_config();
  pcfg.stack_size = 16 * 1024;
  pcfg.thread_name = "worker";
  // The UI loop (Arduino loopTask, priority 1) runs on core 1: the worker must never take its core, or a long job (indexing a
  // book: 7-17 s) freezes the UI. Core 0 and the lowest priority.
  pcfg.prio = 1;
  pcfg.pin_to_core = 0;
  esp_pthread_set_cfg(&pcfg);
  gOs.loadConfig();  // after the storage is mounted
  gOs.setSurface(apps::createSurface("", gOs));  // the launcher
  gOs.setup();
}

void loop() {
  M5.update();
  gInput.update();
  pollSerial();
  gLoop.iterate();
  gRemote.tick();

  // Heartbeat: lets a host script read hardware facts without catching the boot log.
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    POS_LOG("beat", "up=%us board=%d display=%dx%d psram=%u/%u heap=%u battery=%dmV %d%% charging=%d",
            (unsigned)(millis() / 1000), (int)M5.getBoard(), (int)M5.Display.width(), (int)M5.Display.height(),
            (unsigned)ESP.getFreePsram(), (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap(),
            (int)M5.Power.getBatteryVoltage(), (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());
    POS_LOG("sd", "ok=%d size=%uMB free=%uMB data=%d config keys(len): %s", (int)gStorage.sdOk,
            (unsigned)gStorage.sdTotalMb, (unsigned)gStorage.sdFreeMb, (int)gStorage.dataOk, gCfgKeys.c_str());
  }
}
