// Device entry point (Phase 0 spike): draws the shared demo screen on the PaperS3,
// logs hardware facts over USB serial, marks touches with a fast partial refresh.

#include <Arduino.h>
#include <M5Unified.h>

#include "core/demo.h"
#include "core/gfx.h"

using namespace pos;

static Canvas canvas;
static uint32_t bootRefreshMs = 0;
static uint32_t bench[6] = {0};

static void pushRect(const Rect& r, Refresh mode) {
  auto epd = mode == Refresh::Quality ? lgfx::epd_quality : mode == Refresh::Text ? lgfx::epd_text : lgfx::epd_fast;
  M5.Display.setEpdMode(epd);
  M5.Display.setClipRect(r.x, r.y, r.w, r.h);
  canvas.pushSprite(&M5.Display, 0, 0);
  M5.Display.clearClipRect();
}

void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;
  M5.begin(cfg);
  Serial.begin(115200);

  if (M5.Display.width() > M5.Display.height()) M5.Display.setRotation(M5.Display.getRotation() ^ 1);

  Serial.printf("[boot] board=%d display=%dx%d rot=%d\n", (int)M5.getBoard(), (int)M5.Display.width(),
                (int)M5.Display.height(), (int)M5.Display.getRotation());
  Serial.printf("[boot] psram=%u free=%u flash=%u heap=%u\n", (unsigned)ESP.getPsramSize(),
                (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFlashChipSize(), (unsigned)ESP.getFreeHeap());
  Serial.printf("[boot] battery=%dmV level=%d%% charging=%d\n", (int)M5.Power.getBatteryVoltage(),
                (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());

  canvas.setPsram(true);
  canvas.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!canvas.createSprite(kScreenW, kScreenH)) {
    Serial.println("[boot] canvas alloc failed");
    return;
  }
  drawDemo(canvas, "M5PaperS3");

  uint32_t t0 = millis();
  pushRect(Rect{0, 0, kScreenW, kScreenH}, Refresh::Quality);
  M5.Display.waitDisplay();
  bootRefreshMs = millis() - t0;
  Serial.printf("[boot] full quality refresh %ums\n", (unsigned)bootRefreshMs);

  // One-off refresh timing benchmark (results repeat in the heartbeat). Remove with the spike.
  auto timed = [](const Rect& r, Refresh mode) {
    uint32_t s = millis();
    pushRect(r, mode);
    M5.Display.waitDisplay();
    return millis() - s;
  };
  const Rect full{0, 0, kScreenW, kScreenH}, row{0, 400, kScreenW, 96}, box{170, 380, 200, 200};
  drawDemo(canvas, "bench: text full");
  canvas.fillRect(0, 60, kScreenW, 120, kWhite);
  canvas.setFont(&fonts::DejaVu40);
  canvas.setTextDatum(textdatum_t::top_left);
  canvas.drawString("Benchmark", 24, 88);
  bench[0] = timed(full, Refresh::Text);
  canvas.fillRect(row.x, row.y, row.w, row.h, kWhite);
  canvas.drawString("Row update", 24, 420);
  bench[1] = timed(row, Refresh::Text);
  canvas.fillRect(box.x, box.y, box.w, box.h, kBlack);
  bench[2] = timed(box, Refresh::Fast);
  canvas.fillRect(box.x, box.y, box.w, box.h, kWhite);
  bench[3] = timed(box, Refresh::Fast);
  canvas.fillRect(0, 0, kScreenW, kScreenH, kBlack);
  bench[4] = timed(full, Refresh::Fast);
  drawDemo(canvas, "M5PaperS3");
  bench[5] = timed(full, Refresh::Quality);
  Serial.printf("[bench] textFull=%u textRow=%u fastBoxOn=%u fastBoxOff=%u fastFull=%u qualityFull=%u\n",
                (unsigned)bench[0], (unsigned)bench[1], (unsigned)bench[2], (unsigned)bench[3], (unsigned)bench[4],
                (unsigned)bench[5]);
}

void loop() {
  M5.update();
  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    uint32_t t0 = millis();
    Rect r = drawTouchMarker(canvas, t.x, t.y);
    pushRect(r, Refresh::Fast);
    M5.Display.waitDisplay();
    Serial.printf("[touch] x=%d y=%d fast refresh %ums\n", (int)t.x, (int)t.y, (unsigned)(millis() - t0));
  }
  // Heartbeat: lets a host script read hardware facts without catching the boot log.
  static uint32_t lastBeat = 0;
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    Serial.printf("[beat] up=%us board=%d fullRefresh=%ums display=%dx%d psram=%u/%u heap=%u battery=%dmV %d%% charging=%d\n",
                  (unsigned)(millis() / 1000), (int)M5.getBoard(), (unsigned)bootRefreshMs, (int)M5.Display.width(),
                  (int)M5.Display.height(),
                  (unsigned)ESP.getFreePsram(), (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreeHeap(),
                  (int)M5.Power.getBatteryVoltage(), (int)M5.Power.getBatteryLevel(), (int)M5.Power.isCharging());
    Serial.printf("[bench] textFull=%u textRow=%u fastBoxOn=%u fastBoxOff=%u fastFull=%u qualityFull=%u\n",
                  (unsigned)bench[0], (unsigned)bench[1], (unsigned)bench[2], (unsigned)bench[3],
                  (unsigned)bench[4], (unsigned)bench[5]);
  }
  delay(10);
}
