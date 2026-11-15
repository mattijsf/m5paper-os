// Device entry point. Builds the PaperS3 Platform and hands it to pos::Os.
// The HAL classes here are thin (P1-1); P1-2 moves them into display/input/clock/storage.cpp.
// Also keeps the Phase 0 diagnostics: boot report, SD / config-key check, serial heartbeat.

#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "core/gfx.h"
#include "core/log.h"
#include "core/os.h"
#include "hal/platform.h"

using namespace pos;

namespace pos::hal {
void logWrite(const char* line) { Serial.print(line); }
}  // namespace pos::hal

namespace {

class DeviceDisplay : public hal::Display {
 public:
  void push(const Canvas& frame, Rect r, Refresh mode) override {
    auto epd = mode == Refresh::Quality ? lgfx::epd_quality : mode == Refresh::Text ? lgfx::epd_text : lgfx::epd_fast;
    M5.Display.setEpdMode(epd);
    M5.Display.setClipRect(r.x, r.y, r.w, r.h);
    const_cast<Canvas&>(frame).pushSprite(&M5.Display, 0, 0);
    M5.Display.clearClipRect();
  }
  bool busy() override { return M5.Display.displayBusy(); }
  void wait() override { M5.Display.waitDisplay(); }
};

class DeviceInput : public hal::Input {
 public:
  // Call once per loop after M5.update().
  void update() {
    auto t = M5.Touch.getDetail();
    if (t.wasPressed()) queue(hal::TouchEvent::Down, t);
    else if (t.wasReleased()) queue(hal::TouchEvent::Up, t);
    else if (t.isPressed() && (t.deltaX() != 0 || t.deltaY() != 0)) queue(hal::TouchEvent::Move, t);
  }
  bool poll(hal::TouchEvent& out) override {
    if (!has_) return false;
    out = ev_;
    has_ = false;
    return true;
  }

 private:
  void queue(hal::TouchEvent::Type type, const m5::touch_detail_t& t) {
    ev_.type = type;
    ev_.x = t.x;
    ev_.y = t.y;
    ev_.ms = millis();
    has_ = true;
  }
  hal::TouchEvent ev_;
  bool has_ = false;
};

class DeviceClock : public hal::Clock {
 public:
  uint32_t millis() override { return ::millis(); }
  int64_t now() override { return 0; }  // RTC arrives with P1-2
  void set(int64_t) override {}
};

class DeviceNet : public hal::Net {
 public:
  bool wifiConnect(const std::string&, const std::string&, uint32_t) override { return false; }
  void wifiOff() override {}
  bool wifiConnected() override { return false; }
  int rssi() override { return 0; }
  std::vector<hal::WifiNetwork> scan() override { return {}; }
  hal::HttpResponse request(const hal::HttpRequest&) override {
    hal::HttpResponse r;
    r.error = "not implemented on the device yet";
    return r;
  }
};

class DevicePower : public hal::Power {
 public:
  int batteryMv() override { return M5.Power.getBatteryVoltage(); }
  int batteryPercent() override { return M5.Power.getBatteryLevel(); }
  bool charging() override { return M5.Power.isCharging(); }
  hal::Wake lightSleep(uint32_t) override { return hal::Wake::Other; }  // P3-1
  void powerOff(uint32_t) override {}                                   // P3-1
};

DeviceDisplay gDisplay;
DeviceInput gInput;
DeviceClock gClock;
DeviceNet gNet;
DevicePower gPower;
hal::Platform gPlatform{gDisplay, gInput, gClock, gNet, gPower, "M5PaperS3"};
Os gOs(gPlatform);

bool sdOk = false;
String cfgKeys = "(not read)";
uint32_t sdSizeMb = 0, sdFreeMb = 0;

}  // namespace

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.begin(115200);

  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);

  POS_LOG("boot", "board=%d display=%dx%d rot=%d", (int)M5.getBoard(), (int)M5.Display.width(),
          (int)M5.Display.height(), (int)M5.Display.getRotation());
  POS_LOG("boot", "psram=%u free=%u flash=%u heap=%u", (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram(),
          (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getFreeHeap());
  POS_LOG("boot", "battery=%dmV level=%d%% charging=%d", (int)M5.Power.getBatteryVoltage(),
          (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());

  // SD card check (pins: docs/HARDWARE.md). Mounted at /sd. Real storage code arrives with P1-2.
  SPI.begin(39, 40, 38, 47);
  sdOk = SD.begin(47, SPI, 25000000, "/sd");
  if (sdOk) {
    sdSizeMb = (uint32_t)(SD.totalBytes() / (1024 * 1024));
    sdFreeMb = (uint32_t)((SD.totalBytes() - SD.usedBytes()) / (1024 * 1024));
  }
  POS_LOG("boot", "sd=%d type=%d size=%uMB free=%uMB", (int)sdOk, (int)SD.cardType(), (unsigned)sdSizeMb,
          (unsigned)sdFreeMb);

  // Provisioning file check: report key names and value lengths only, never values.
  if (sdOk) {
    File f = SD.open("/paperos_config", FILE_READ);
    if (!f) {
      cfgKeys = "(file missing)";
    } else {
      cfgKeys = "";
      while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        int eq = line.indexOf('=');
        if (line.length() == 0 || line[0] == '#' || eq <= 0) continue;
        cfgKeys += line.substring(0, eq) + "(" + String(line.length() - eq - 1) + ") ";
      }
      f.close();
    }
  }

  gOs.setup();
}

void loop() {
  M5.update();
  gInput.update();
  gOs.loopOnce();

  // Heartbeat: lets a host script read hardware facts without catching the boot log.
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    POS_LOG("beat", "up=%us board=%d display=%dx%d psram=%u/%u heap=%u battery=%dmV %d%% charging=%d",
            (unsigned)(millis() / 1000), (int)M5.getBoard(), (int)M5.Display.width(), (int)M5.Display.height(),
            (unsigned)ESP.getFreePsram(), (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap(),
            (int)M5.Power.getBatteryVoltage(), (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());
    POS_LOG("sd", "ok=%d size=%uMB free=%uMB config keys(len): %s", (int)sdOk, (unsigned)sdSizeMb, (unsigned)sdFreeMb,
            cfgKeys.c_str());
  }
  delay(10);
}
