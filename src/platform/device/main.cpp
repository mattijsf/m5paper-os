// Device entry point. Builds the PaperS3 Platform and hands it to pos::Os.
// Display, input, clock and storage live in display/input/clock/storage.cpp; Net and Power are
// still stubs here (P3). Also keeps the boot report, SD config-key check and serial heartbeat.

#include <Arduino.h>
#include <M5Unified.h>
#include <LittleFS.h>
#include <SD.h>
#include <esp_mac.h>
#include <esp_pthread.h>

#include "platform/posix/http_server.h"
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_rom_crc.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>

#include "core/fs.h"
#include "core/gfx.h"
#include "core/log.h"
#include "apps/apps.h"
#include "core/loop.h"
#include "core/os.h"
#include "core/remote.h"
#include "core/update.h"
#include "platform/posix/remote_server.h"
#include "core/sound.h"
#include "ui/text.h"

#include "hal/platform.h"
#include "hal/system.h"
#include "platform/device/device.h"

using namespace pos;

namespace {
// Log lines wait while a remote reply leaves in pieces (RemoteHost::replyLeaving): the UI thread and the worker both log.
std::mutex gLogMutex;
bool gHoldLog = false;
std::string gHeldLog;

void holdLog(bool on) {
  std::lock_guard<std::mutex> lock(gLogMutex);
  gHoldLog = on;
  if (!on && !gHeldLog.empty()) {
    Serial.print(gHeldLog.c_str());
    gHeldLog.clear();
    gHeldLog.shrink_to_fit();
  }
}
}  // namespace

namespace pos::hal {
void logWrite(const char* line) {
  std::lock_guard<std::mutex> lock(gLogMutex);
  if (gHoldLog) {
    if (gHeldLog.size() < 8192) gHeldLog += line;  // the log ring (>log) has every line anyway
    return;
  }
  Serial.print(line);
}

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
void restart() {
  device::confirmUpdateIfPending();
  esp_restart();
}
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
platform::PosixHttpServer gHttp(80);
device::Updater gUpdater;
hal::Platform gPlatform{gDisplay, gInput, gClock, gNet, gPower, "M5PaperS3", &gHttp, &gUpdater};
Os gOs(gPlatform);

// Idle step: PowerPolicy light-sleeps when everything allows it (core/power_policy.h), else a short delay.
PowerPolicy gPolicy(gOs, [](uint32_t ms) {
  if (ms > 0) delay(ms < 10 ? ms : 10);
});
Loop gLoop(gOs, gPolicy);

// Remote control over the USB serial port (lines starting with '>', see core/remote.h).
// Memory report (D27, tools/memory.py): internal heap free / lowest since boot / largest free block (fragmentation: TLS
// needs one large block), the same for PSRAM, and the unused stack of each known task (high-water mark, bytes).
void memReport(const std::string& tag) {
  const uint32_t iFree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL), iMin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 iBig = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  const uint32_t pFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), pMin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                 pBig = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  std::string stacks;
  for (const char* name : {"loopTask", "worker", "epd", "epdtile", "wifi", "tiT", "sys_evt", "async_tcp", "ipc0", "ipc1", "esp_timer"}) {
    if (TaskHandle_t t = xTaskGetHandle(name)) {
      char b[40];
      snprintf(b, sizeof b, " %s=%u", name, static_cast<unsigned>(uxTaskGetStackHighWaterMark(t)));
      stacks += b;
    }
  }
  POS_LOG("mem", "%s internal free=%u min=%u largest=%u psram free=%u min=%u largest=%u", tag.c_str(),
          static_cast<unsigned>(iFree), static_cast<unsigned>(iMin), static_cast<unsigned>(iBig), static_cast<unsigned>(pFree),
          static_cast<unsigned>(pMin), static_cast<unsigned>(pBig));
  POS_LOG("memstack", "%s%s", tag.c_str(), stacks.c_str());  // a separate line: the log line is at most 256 characters
}

// PSRAM soak test (D25: 120 MHz PSRAM stays only if this finds nothing). ">set psram-test 600" starts it; each main-loop
// pass then writes a pseudo-random pattern (xorshift, a new seed per pass) over 2 MB of PSRAM and reads it back, while the UI
// keeps running (taps, waveforms). Every pass with errors is logged, and a summary at the end:
// "[psram] test: 2048 KB x N passes in Ss, E words wrong, R MB/s".
float gBootTempC = 0;  // on-chip temperature at boot: 120 MHz PSRAM/flash timing is tuned near this (D25)
float gTempGuardC = 12.0f;  // drift from gBootTempC that restarts the board (loop(); ">set temp-guard N" to test)

struct PsramSoak {
  static constexpr size_t kWords = 2 * 1024 * 1024 / 4;
  // Flash reads too (120 MHz flash, D25): each pass reads the next 512 KB of the running app partition; the first read of a
  // chunk sets its CRC, every later read must give the same.
  static constexpr size_t kChunk = 512 * 1024, kChunks = 6;
  uint32_t crc[kChunks] = {};
  bool crcSet[kChunks] = {};
  uint32_t flashErrors = 0;
  float tStart = 0, tMin = 999, tMax = -999;  // chip temperature (on-chip sensor), D25: 120 MHz PSRAM is tuned at boot
  uint32_t* buf = nullptr;
  uint32_t until = 0, t0 = 0, passes = 0, errors = 0, busyMs = 0;
  uint64_t bytes = 0;
  void start(uint32_t seconds) {
    if (buf) return;
    buf = static_cast<uint32_t*>(heap_caps_malloc(kWords * 4, MALLOC_CAP_SPIRAM));
    if (!buf) {
      POS_LOG("psram", "test: no memory");
      return;
    }
    t0 = millis();
    until = t0 + seconds * 1000u;
    passes = errors = busyMs = flashErrors = 0;
    bytes = 0;
    for (auto& c : crcSet) c = false;
    tStart = temperatureRead();
    tMin = tMax = tStart;
    POS_LOG("psram", "test: started for %us", static_cast<unsigned>(seconds));
  }
  void step() {
    if (!buf) return;
    const uint32_t p0 = millis();
    const uint32_t seed = 0x9E3779B9u ^ (passes * 0x85EBCA6Bu) ^ 1u;
    uint32_t x = seed;
    for (size_t i = 0; i < kWords; ++i) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      buf[i] = x;
    }
    x = seed;
    uint32_t bad = 0;
    for (size_t i = 0; i < kWords; ++i) {
      x ^= x << 13; x ^= x >> 17; x ^= x << 5;
      if (buf[i] != x) ++bad;
    }
    busyMs += millis() - p0;
    bytes += kWords * 8;
    ++passes;
    if (bad) POS_LOG("psram", "pass %u: %u words wrong", static_cast<unsigned>(passes), static_cast<unsigned>(bad));
    errors += bad;
    if (passes % 8 == 0) {
      const float t = temperatureRead();
      tMin = std::min(tMin, t);
      tMax = std::max(tMax, t);
    }
    if (const esp_partition_t* app = esp_ota_get_running_partition()) {
      const size_t k = passes % kChunks;
      if ((k + 1) * kChunk <= app->size && esp_partition_read(app, k * kChunk, buf, kChunk) == ESP_OK) {  // buf: reused
        const uint32_t c = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(buf), kChunk);
        if (!crcSet[k]) {
          crc[k] = c;
          crcSet[k] = true;
        } else if (c != crc[k]) {
          ++flashErrors;
          POS_LOG("psram", "pass %u: flash chunk %u read differently", static_cast<unsigned>(passes), static_cast<unsigned>(k));
        }
      }
    }
    if (static_cast<int32_t>(millis() - until) >= 0) {
      heap_caps_free(buf);
      buf = nullptr;
      POS_LOG("psram", "test: 2048 KB x %u passes in %us, %u words wrong, %u MB/s (write + read); flash re-reads: %u differed; "
              "chip %.1f C at the start, %.1f..%.1f C, %.1f C at boot",
              static_cast<unsigned>(passes), static_cast<unsigned>((millis() - t0) / 1000), static_cast<unsigned>(errors),
              static_cast<unsigned>(busyMs ? bytes / 1000 / busyMs : 0), static_cast<unsigned>(flashErrors), tStart, tMin, tMax,
              gBootTempC);
    }
  }
};
PsramSoak gSoak;

// Writes a test pattern into the panel's frame buffer twice, row by row (h = 1: M5GFX's stock writeImage) and as one rect
// (the tiled writeImage of tools/patch_m5gfx.py), in Fast and Quality modes, and compares what the driver holds. Then the
// UI is pushed again. The result is logged: "[epdtest] fast identical ...".
void epdSelfTest() {
  constexpr int W = 540, H = 240, Y = 300;
  Canvas t;
  t.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  t.setPsram(true);
  if (!t.createSprite(W, H)) {
    POS_LOG("epdtest", "no memory");
    return;
  }
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) t.drawPixel(x, y, gray(((x * 16) / W + y / 15) % 16) ^ ((x ^ y) & 1 ? 0x080808u : 0));
  pos::ui::drawText(t, "Tiled write test 0123 AVAWAY", 20, 100, pos::ui::Type::Headline, kBlack, pos::ui::Anchor::TopLeft, kWhite);
  std::vector<uint16_t> a(W * H), b(W * H);
  M5.Display.waitDisplay();
  for (auto mode : {lgfx::epd_fast, lgfx::epd_quality}) {
    M5.Display.setEpdMode(mode);
    M5.Display.startWrite();
    M5.Display.fillRect(0, Y, W, H, TFT_WHITE);
    for (int y = 0; y < H; ++y) {
      M5.Display.setClipRect(0, Y + y, W, 1);
      t.pushSprite(&M5.Display, 0, Y);
    }
    M5.Display.clearClipRect();
    M5.Display.readRect(0, Y, W, H, a.data());
    M5.Display.fillRect(0, Y, W, H, TFT_WHITE);
    const uint32_t t0 = millis();
    t.pushSprite(&M5.Display, 0, Y);
    const uint32_t dt = millis() - t0;
    M5.Display.readRect(0, Y, W, H, b.data());
    M5.Display.endWrite();
    int diff = 0;
    for (int i = 0; i < W * H; ++i) diff += a[i] != b[i];
    POS_LOG("epdtest", "%s %s (%d of %d pixels differ), rect write %u ms", mode == lgfx::epd_fast ? "fast" : "quality",
            diff ? "DIFFERENT" : "identical", diff, W * H, static_cast<unsigned>(dt));
  }
  M5.Display.waitDisplay();
  t.deleteSprite();
  pos::ui::forgetLoadedFont();
  gOs.displayManager().forceClean();  // the UI back on the panel
}

// Platform debug switches (">set key value"), over USB and over Wi-Fi alike.
bool deviceOption(const std::string& key, const std::string& value) {
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
  if (key == "gray-pass") {  // "off": a screen with gray areas (the launcher's icon wells) goes out Fast then settle, not as
                             // one Text pass (DisplayParams::grayAreaMin = 0): to compare the two by eye
    DisplayParams p = gOs.displayManager().params();
    p.grayAreaMin = value == "off" ? 0 : DisplayParams{}.grayAreaMin;
    gOs.displayManager().setParams(p);
    return true;
  }
  if (key == "epd-keep") {  // tools/patch_m5gfx.py patch 4: off = the stock rule, for a comparison on the panel
    gDisplay.setKeepUnchanged(value != "off");
    return true;
  }
  if (key == "idle-min") {  // ">set idle-min 1": minutes until the idle check acts (0 = never) until the next boot, for a
    gOs.config().power.autoOffMin = atoi(value.c_str());  // test of the sleep screen, or a camera session; not saved
    return true;
  }
  if (key == "chime") {  // ">set chime 1": the sound for new messages, whatever the setting (core/sound.h)
    sound::play();
    return true;
  }
  if (key == "memreport") {  // ">set memreport": one "[mem]" line (tools/memory.py), D27
    memReport(value);
    return true;
  }
  if (key == "temp-guard") {  // test aid: ">set temp-guard 1" makes the drift restart easy to trigger (D25)
    gTempGuardC = static_cast<float>(atof(value.c_str()));
    return true;
  }
  if (key == "psram-test") {  // ">set psram-test 60": D25, a soak test for PSRAM speed settings
    gSoak.start(static_cast<uint32_t>(atoi(value.c_str())));
    return true;
  }
  if (key == "epd-selftest") {  // tools/patch_m5gfx.py: the patched frame buffer write must equal the stock one
    epdSelfTest();
    return true;
  }
  return false;
}

class SerialRemoteHost : public RemoteHost {
 public:
  void inject(const hal::TouchEvent& ev) override { gInput.inject(ev); }
  size_t writable() override { return static_cast<size_t>(std::max(0, Serial.availableForWrite())); }
  void replyLeaving(bool on) override { holdLog(on); }
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
  bool option(const std::string& key, const std::string& value) override { return deviceOption(key, value); }
  void reboot() override {
    Serial.flush();
    delay(100);
    esp_restart();
  }
};
SerialRemoteHost gRemoteHost;
Remote gRemote(gOs, gClock, gRemoteHost);
// Wireless development (D39): the same commands over Wi-Fi, from the Mac that holds the update key.
platform::RemoteServer gWireless(gOs, gClock, update::kPort, true,
                                 {[](const hal::TouchEvent& ev) { gInput.inject(ev); }, deviceOption, [] {
                                    delay(100);
                                    esp_restart();
                                  }});
// Command lines and upload bytes alike go to the remote, which tells them apart (Remote::feed).
void pollSerial() {
  static uint8_t* buf = static_cast<uint8_t*>(malloc(4096));  // a heap block: PSRAM, not internal RAM (D27)
  if (!buf) return;
  while (Serial.available() > 0) {
    const size_t n = Serial.readBytes(reinterpret_cast<char*>(buf), std::min<size_t>(4096, Serial.available()));
    if (n == 0) break;
    gRemote.feed(buf, n);
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

// The loop task's stack (Arduino default 8 KB): an Ed25519 check of a valid signature (wireless development's sign-in, an
// update's header: core/update.h) ran it out and restarted the board (2026-11-19). 16 KB, without rebuilding ESP-IDF.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.setRxBufferSize(16384);  // uploads arrive in 4 KB bursts
  Serial.setTxBufferSize(16384);  // frame dumps leave in pieces between loop passes (Remote::pump): room for one pass's worth
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block the UI thread on a USB host that is not reading the log

  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);

  POS_LOG("boot", "board=%d display=%dx%d rot=%d", (int)M5.getBoard(), (int)M5.Display.width(),
          (int)M5.Display.height(), (int)M5.Display.getRotation());
  POS_LOG("boot", "psram=%u free=%u flash=%u heap=%u", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram(),
          (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getFreeHeap());
  gBootTempC = temperatureRead();
  POS_LOG("boot", "chip temperature %.1f C (PSRAM and flash timing are tuned at boot)", gBootTempC);
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
  // book: 7-17 s) freezes the UI. Core 0 and the lowest priority esp_pthread accepts (1; 0 is rejected and then the whole
  // config, core and stack included, would be ignored).
  pcfg.prio = 1;
  pcfg.pin_to_core = 0;
  esp_pthread_set_cfg(&pcfg);
  gOs.loadConfig();  // after the storage is mounted
  gOs.setSurface(apps::createBootSurface("", gOs));  // the launcher, or the app that was open at the last power-off
  gOs.setup();
}

void loop() {
  M5.update();
  gInput.update();
  pollSerial();
  gLoop.iterate();
  gRemote.tick();
  gWireless.poll();
  gSoak.step();  // ">set psram-test N" (D25)

  // Heartbeat: lets a host script read hardware facts without catching the boot log.
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    // D25: flash and PSRAM run at 120 MHz with timing tuned at boot; ESP-IDF warns that a drift of about 20 C from the boot
    // temperature can make accesses fail. A drift of more than 12 C restarts the board (the boot tunes again) once nobody has
    // touched it for 30 s and no PSRAM test runs. App state is saved as it changes, the panel keeps its image.
    const float t = temperatureRead();
    if (std::fabs(t - gBootTempC) > gTempGuardC && millis() - gOs.lastInputMs() > 30000 && !gSoak.buf) {
      POS_LOG("boot", "chip at %.1f C, %.1f C at boot: restarting to tune the 120 MHz memory timing again", t, gBootTempC);
      if (Surface* s = gOs.surface()) s->prepareForPowerOff();
      M5.Display.waitDisplay();
      Serial.flush();
      device::confirmUpdateIfPending();  // the firmware's own restart, not a failed start (hal/update.h)
      esp_restart();
    }
    POS_LOG("beat", "up=%us board=%d display=%dx%d psram=%u/%u heap=%u battery=%dmV %d%% charging=%d temp=%.1fC",
            (unsigned)(millis() / 1000), (int)M5.getBoard(), (int)M5.Display.width(), (int)M5.Display.height(),
            (unsigned)ESP.getFreePsram(), (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap(),
            (int)M5.Power.getBatteryVoltage(), (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging(), temperatureRead());
    POS_LOG("sd", "ok=%d size=%uMB free=%uMB data=%d config keys(len): %s", (int)gStorage.sdOk,
            (unsigned)gStorage.sdTotalMb, (unsigned)gStorage.sdFreeMb, (int)gStorage.dataOk, gCfgKeys.c_str());
  }
}
