// Weather: the Google Weather integration's entity in Home Assistant (docs/apps/weather.md). One screen: now (condition,
// temperature, feels like, wind, humidity, rain), the next hours and the next days. The saved copy shows first, then the
// entity and its two forecasts (weather.get_forecasts, daily and hourly) are read on the worker. Read only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "apps/weather/weather_model.h"
#include "core/app.h"
#include "core/fs.h"
#include "core/timefmt.h"
#include "ui/root.h"
#include "ui/text.h"
#include "ui/widgets.h"

namespace pos::weather {

namespace {

using ui::Anchor;
using ui::Type;

constexpr const char* kCacheFile = "/data/cache/weather/weather.json";
constexpr const char* kDefaultEntity = "weather.thuis";  // Google Weather, home zone (home-assistant packages/google_neerslag.yaml)
constexpr const char* kDefaultRain = "sensor.google_neerslag_minuten";  // Google's minute rain forecast (same package)
constexpr uint32_t kRefreshMs = 15 * 60 * 1000;          // read again while the screen stays open, and on return after this long
constexpr int kHours = 8, kHourStride = 2;               // 8 columns, two hours apart: the next 16 hours
constexpr int kDays = 5;
constexpr int64_t kOldNowSec = 90 * 60;                  // a saved "now" older than this gives way to the hour's forecast

bool isNight(const Step& s) {
  if (s.daytime >= 0) return s.daytime == 0;
  struct tm tmv;
  if (!localTime(s.time, tmv)) return false;
  return tmv.tm_hour < 7 || tmv.tm_hour >= 19;
}

std::string hourText(int64_t t) {
  struct tm tmv;
  if (!localTime(t, tmv)) return "";
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return buf;
}

std::string dayText(int64_t t, int64_t now) {
  static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  const int d = localDay(t);
  if (now > 0 && d == localDay(now)) return "Today";
  if (now > 0 && d == localDay(now) + 1) return "Tomorrow";
  struct tm tmv;
  if (!localTime(t, tmv)) return "";
  return std::string(kDays[tmv.tm_wday]) + " " + std::to_string(tmv.tm_mday);
}

std::string percent(double v) { return v == kNone ? "" : std::to_string(std::lround(v)) + "%"; }

// "13:34" today, "Wed 13:34" on another day.
std::string whenText(int64_t t, int64_t now) {
  if (t <= 0) return "";
  if (now > 0 && localDay(t) == localDay(now)) return hourText(t);
  return dayText(t, 0) + " " + hourText(t);
}

// The whole forecast drawn in one widget: four bands one above the other (docs/apps/weather.md "Screen").
class WeatherView : public ui::Widget {
 public:
  // Band tops inside the frame.
  static constexpr int kRainTop = 168, kHoursTop = 368, kDaysTop = 516;
  void set(const Weather& w, int64_t now) {
    w_ = w;
    now_ = now;
    hours_ = nextHours(w.hourly, now, kHours, kHourStride);
    days_ = nextDays(w.daily, now, kDays);
    invalidate();
  }
  ui::Size measure(int availW) override { return {availW, 760}; }

  void draw(Canvas& c) override {
    const int x = frame.x, y = frame.y, w = frame.w;
    drawNow(c, x, y + 8, w);
    c.drawFastHLine(x, y + kRainTop - 8, w, ui::divider);
    drawRain(c, x, y + kRainTop, w);
    c.drawFastHLine(x, y + kHoursTop - 12, w, ui::divider);
    drawHours(c, x, y + kHoursTop, w);
    c.drawFastHLine(x, y + kDaysTop - 12, w, ui::divider);
    drawDays(c, x, y + kDaysTop, w, frame.y + frame.h);
  }

  // A tap on the rain graph switches between the next 2 and the next 6 hours.
  bool onTouch(const Gesture& g) override {
    const bool inRain = g.y >= frame.y + kRainTop - 8 && g.y < frame.y + kHoursTop - 12;
    if (!inRain) return false;
    if (g.type == Gesture::Tap) {
      rainHours_ = rainHours_ == 2 ? 6 : 2;
      invalidateRect(Rect{static_cast<int16_t>(frame.x), static_cast<int16_t>(frame.y + kRainTop - 6),
                          static_cast<int16_t>(frame.w), static_cast<int16_t>(kHoursTop - kRainTop - 8)},
                     Hint::Content);
    }
    return g.type == Gesture::Down || g.type == Gesture::Tap;
  }
  int rainHours() const { return rainHours_; }
  void setRainHours(int h) { rainHours_ = h; }

 private:
  void drawNow(Canvas& c, int x, int y, int w) {
    const Current& n = w_.now;
    const Step* hour = hours_.empty() ? nullptr : &hours_.front();
    // A saved copy from long ago: its "now" is old, the forecast for this hour is the better guess (docs/apps/weather.md "Stale").
    const bool old = hour && now_ > 0 && w_.fetched > 0 && now_ - w_.fetched > kOldNowSec;
    const std::string condition = old ? hour->condition : n.condition;
    const double temp = old ? hour->temperature : n.temperature;
    const bool night = condition == "clear-night" || (hour && isNight(*hour));
    ui::drawIcon(c, conditionIcon(condition, night), x + 32, y + 52, 64, ui::ink);
    ui::drawText(c, temp == kNone ? "--" : tempText(temp), x + 84, y + 8, Type::Headline, ui::ink);
    ui::drawText(c, ui::ellipsize(c, conditionText(condition), Type::BodyStrong, 296 - 84), x + 84, y + 66, Type::BodyStrong, ui::ink);
    if (old) ui::drawText(c, "Forecast for " + hourText(hour->time), x + 84, y + 100, Type::Body, ui::secondary);
    else if (n.apparent != kNone) ui::drawText(c, "Feels like " + tempText(n.apparent), x + 84, y + 100, Type::Body, ui::ink);
    // Right column: wind, humidity (left out when they are old), rain in the coming hour.
    const int rx = x + 300;
    int row = 0;
    auto line = [&](Icon icon, const std::string& text) {
      const int cy = y + 20 + row++ * 48;
      ui::drawIcon(c, icon, rx + 16, cy, 32, ui::ink);
      ui::drawText(c, ui::ellipsize(c, text, Type::Body, x + w - rx - 44), rx + 44, cy, Type::Body, ui::ink, Anchor::MiddleLeft);
    };
    if (!old) {
      std::string wind = "--";
      if (n.windSpeed != kNone) {
        wind = std::to_string(std::lround(n.windSpeed)) + " " + n.windUnit;
        const std::string dir = compass(n.windBearing);
        if (!dir.empty()) wind = dir + " " + wind;
      }
      line(Icon::Wind, wind);
      line(Icon::Droplets, n.humidity == kNone ? "--" : percent(n.humidity));
    }
    std::string rain = "--";
    if (hour && hour->precipProbability != kNone) {
      rain = percent(hour->precipProbability);
      if (hour->precipitation > 0) rain += "  " + amount(hour->precipitation, n.precipUnit);
    }
    line(Icon::Umbrella, rain);
  }

  // Google's minute forecast like the house dashboard's rain card: headline, rate now and peak, the rain area on a log scale
  // with the light / moderate / heavy lines, "Now" at the left edge.
  void drawRain(Canvas& c, int x, int y, int w) {
    const bool fresh = rainFresh(w_.rain, w_.rainUpdated, now_);
    ui::drawIcon(c, Icon::Umbrella, x + 16, y + 14, 32, ui::ink);
    ui::drawText(c, fresh ? rainHeadline(w_.rain, now_, rainHours_) : "No current rain forecast", x + 44, y + 14, Type::BodyStrong,
                 ui::ink, Anchor::MiddleLeft);
    if (fresh) {
      const double nowRate = rainNow(w_.rain, now_), peak = rainPeak(w_.rain, now_, rainHours_);
      std::string facts = "Now " + rateText(nowRate);
      if (peak > nowRate) facts += ", max " + rateText(peak);
      ui::drawText(c, facts, x + w, y + 14, Type::Caption, ui::ink, Anchor::MiddleRight);
    }
    const int plotW = w - 92, top = y + 64, base = y + 176;
    const int64_t from = now_, to = now_ + rainHours_ * 3600;
    auto px = [&](int64_t t) { return x + static_cast<int>((t - from) * plotW / (to - from)); };
    auto py = [&](double h) { return base - static_cast<int>(std::lround(h * (base - top))); };
    // Time ticks above the plot: every half hour (2 h) or hour (6 h).
    ui::drawText(c, "Now", x, y + 36, Type::Caption, ui::secondary);
    const int64_t step = rainHours_ == 2 ? 1800 : 3600;
    if (now_ > 0)
      for (int64_t t = (from / step + 1) * step; t < to; t += step) {
        const int tx = px(t);
        if (tx < x + 56 || tx > x + plotW - 24) continue;
        ui::drawText(c, clockText(t), tx, y + 36, Type::Caption, ui::secondary, Anchor::TopCenter);
        c.drawFastVLine(tx, top - 4, 6, ui::ink);
      }
    if (fresh) {
      const std::vector<float> curve = rainCurve(w_.rain, from, to, plotW);
      for (int i = 0; i < plotW; ++i) {
        if (curve[i] <= 0) continue;
        const int yy = py(curve[i]);
        if (yy >= base) continue;
        c.drawFastVLine(x + i, yy, base - yy, gray(6));
        c.fillRect(x + i, yy, 1, 2, ui::ink);  // the outline
      }
    }
    // Light, moderate, heavy: dotted lines over the area, the names right of the plot.
    static const struct {
      double rate;
      const char* name;
    } kLevels[] = {{0.5, "light"}, {2.5, "moderate"}, {10, "heavy"}};
    for (const auto& l : kLevels) {
      const int ly = py(rainHeight(l.rate));
      for (int i = 0; i < plotW; i += 6) c.drawFastHLine(x + i, ly, 3, ui::divider);
      ui::drawText(c, l.name, x + plotW + 8, ly, Type::Caption, ui::secondary, Anchor::MiddleLeft);
    }
    c.fillRect(x, base, plotW, 2, ui::ink);
    c.fillRect(x, top - 4, 2, base - top + 4, ui::ink);  // now
  }

  void drawHours(Canvas& c, int x, int y, int w) {
    if (hours_.empty()) {
      ui::drawText(c, "No hourly forecast", x, y + 40, Type::Body, ui::ink);
      return;
    }
    const int n = kHours;
    for (int i = 0; i < static_cast<int>(hours_.size()); ++i) {
      const Step& s = hours_[i];
      const int cx = x + (2 * i + 1) * w / (2 * n);
      ui::drawText(c, i == 0 ? "Now" : hourText(s.time), cx, y, Type::Caption, ui::secondary, Anchor::TopCenter);
      ui::drawIcon(c, conditionIcon(s.condition, isNight(s)), cx, y + 50, 32, ui::ink);
      ui::drawText(c, tempText(s.temperature), cx, y + 72, Type::Body, ui::ink, Anchor::TopCenter);
      if (s.precipProbability >= 1) ui::drawText(c, percent(s.precipProbability), cx, y + 102, Type::Caption, ui::ink, Anchor::TopCenter);
    }
  }

  void drawDays(Canvas& c, int x, int y, int w, int bottom) {
    if (days_.empty()) {
      ui::drawText(c, "No daily forecast", x, y + 20, Type::Body, ui::ink);
      return;
    }
    constexpr int kRow = 54;
    double lo = 1e9, hi = -1e9;
    for (const Step& d : days_) {
      if (d.templow != kNone) lo = std::min(lo, d.templow);
      if (d.temperature != kNone) hi = std::max(hi, d.temperature);
      if (d.templow != kNone) hi = std::max(hi, d.templow);
      if (d.temperature != kNone) lo = std::min(lo, d.temperature);
    }
    const int barX = x + 328, barW = 112;
    int top = y - 4;
    for (size_t i = 0; i < days_.size() && top + kRow <= bottom; ++i, top += kRow) {
      const Step& d = days_[i];
      const int cy = top + kRow / 2;
      ui::drawText(c, dayText(d.time, now_), x, cy, Type::BodyStrong, ui::ink, Anchor::MiddleLeft);
      ui::drawIcon(c, conditionIcon(d.condition), x + 148, cy, 32, ui::ink);
      if (d.precipProbability >= 1) {
        const bool mm = d.precipitation > 0;
        ui::drawText(c, percent(d.precipProbability), x + 180, mm ? cy - 11 : cy, Type::Caption, ui::ink, Anchor::MiddleLeft);
        if (mm) ui::drawText(c, amount(d.precipitation, w_.now.precipUnit), x + 180, cy + 11, Type::Caption, ui::ink, Anchor::MiddleLeft);
      }
      ui::drawText(c, tempText(d.templow), barX - 12, cy, Type::Body, ui::ink, Anchor::MiddleRight);
      ui::drawText(c, tempText(d.temperature), x + w, cy, Type::BodyStrong, ui::ink, Anchor::MiddleRight);
      // The day's range on the scale of all shown days.
      c.fillRoundRect(barX, cy - 4, barW, 8, 4, ui::fill);
      if (d.templow != kNone && d.temperature != kNone && hi > lo) {
        const int a = barX + static_cast<int>(std::lround((d.templow - lo) / (hi - lo) * (barW - 8)));
        const int b = barX + 8 + static_cast<int>(std::lround((d.temperature - lo) / (hi - lo) * (barW - 8)));
        c.fillRoundRect(a, cy - 4, std::max(8, b - a), 8, 4, ui::ink);
      }
      if (i + 1 < days_.size() && top + 2 * kRow <= bottom) c.drawFastHLine(x, top + kRow, w, ui::divider);
    }
  }

  Weather w_;
  int64_t now_ = 0;
  int rainHours_ = 2;
  std::vector<Step> hours_, days_;
};

struct Fetched {
  HaError error = HaError::None;  // of the entity's state; forecasts that fail keep the saved ones
  bool parsed = false, daily = false, hourly = false, rain = false;
  Weather weather;
};

class WeatherScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Weather");
    setAction(Icon::RefreshCw, [this] { refresh(); });
    entity_ = ctx().store.get("entity", kDefaultEntity);
    rainEntity_ = ctx().store.get("rainEntity", kDefaultRain);
    std::string cached;
    if (fs::readFile(kCacheFile, cached)) fromJson(cached, weather_);
    holder_ = root.add<ui::Column>(0);
    holder_->flex = 1;
    status_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    root.add<ui::Spacer>(ui::s1);
    showStatus();
    rebuild();
  }

  void onShow() override {
    if (!fetchedOnce_ || ctx().clock.millis() - lastFetchMs_ >= kRefreshMs) refresh();
  }

  std::string debugState() const override {
    return std::string(weather_.valid() ? "weather=" + weather_.now.condition : "weather=none") + "|" + status_->text() +
           (error_ != HaError::None ? std::string("|error=") + haErrorText(error_) : "") +
           (view_ ? "|rain=" + std::to_string(view_->rainHours()) + "h" : "");
  }

 private:
  void rebuild() {
    const int rainHours = view_ ? view_->rainHours() : 2;  // a read keeps the chosen window
    while (!holder_->children().empty()) holder_->remove(holder_->children().back().get());
    view_ = nullptr;
    if (weather_.valid()) {
      view_ = holder_->add<WeatherView>();
      view_->flex = 1;
      view_->setRainHours(rainHours);
      view_->set(weather_, ctx().clock.now());
    } else if (error_ == HaError::NoToken || error_ == HaError::NoUrl) {
      holder_->add<ui::EmptyState>(Icon::Cloud, "Not set up yet", "Put HA_URL and HA_TOKEN in paperos_config on the SD card.")->flex = 1;
    } else if (notFound_) {
      holder_->add<ui::EmptyState>(Icon::Cloud, "No weather in Home Assistant", entity_ + " was not found or has no data.", "Retry",
                                   [this] { refresh(); })->flex = 1;
    } else if (error_ != HaError::None) {
      const std::string hint = error_ == HaError::Other ? std::string("Check Wi-Fi and the address, then try again.")
                                                        : haErrorText(error_) + std::string(". Try again later.");
      holder_->add<ui::EmptyState>(Icon::WifiOff, "Cannot reach Home Assistant", hint, "Retry", [this] { refresh(); })->flex = 1;
    } else {
      holder_->add<ui::EmptyState>(Icon::Cloud, "Loading...")->flex = 1;
    }
    if (holder_->host()) holder_->host()->relayout();
  }

  void showStatus() {
    const int64_t now = ctx().clock.now();
    if (!weather_.valid()) return status_->setText("");
    const std::string when = whenText(weather_.fetched, now);
    if (error_ != HaError::None || notFound_) {
      const char* why = notFound_ ? "no weather in Home Assistant" : haErrorText(error_);
      status_->setText("Saved data" + (when.empty() ? std::string() : " from " + when) + ": " + why);
    } else if (busy_) {
      status_->setText("Updating...");
    } else {
      status_->setText(when.empty() ? "Google Weather" : "Updated " + when);
    }
  }

  void armTimer() {
    if (timerArmed_) return;
    timerArmed_ = true;
    ctx().timer(kRefreshMs, [this] {
      timerArmed_ = false;
      refresh();
    });
  }

  void refresh() {
    if (busy_) return;
    busy_ = true;
    showStatus();
    HaClient ha = ctx().ha();
    const std::string entity = entity_, rainEntity = rainEntity_;
    const Weather old = weather_;
    const int64_t now = ctx().clock.now();
    ctx().async(
        [ha, entity, rainEntity, old, now]() mutable {
          Fetched f;
          HaResult st = ha.get("/api/states/" + entity);
          f.error = st.error;
          if (!st.ok()) return f;
          f.weather = old;
          if (!parseState(st.body, f.weather.now)) {
            f.error = HaError::NotFound;  // no weather entity, or it is unavailable
            return f;
          }
          f.parsed = true;
          f.weather.fetched = now;
          const std::string body = "{\"entity_id\":\"" + entity + "\",\"type\":\"";
          std::vector<Step> steps;
          HaResult daily = ha.callService("weather", "get_forecasts", body + "daily\"}", true);
          if (daily.ok() && parseForecast(daily.body, entity, steps)) {
            f.weather.daily = steps;
            f.daily = true;
          }
          HaResult hourly = ha.callService("weather", "get_forecasts", body + "hourly\"}", true);
          if (hourly.ok() && parseForecast(hourly.body, entity, steps)) {
            f.weather.hourly = steps;
            f.hourly = true;
          }
          // The minute rain forecast is a sensor of the house's own (it may be missing: then the graph says so).
          std::vector<RainBlock> blocks;
          int64_t rainUpdated = 0;
          HaResult rain = ha.get("/api/states/" + rainEntity);
          if (rain.ok() && parseRain(rain.body, blocks, rainUpdated)) {
            f.weather.rain = blocks;
            f.weather.rainUpdated = rainUpdated;
            f.rain = true;
          }
          if (f.parsed) fs::writeFile(kCacheFile, toJson(f.weather));
          return f;
        },
        [this](Fetched f) {
          busy_ = false;
          fetchedOnce_ = true;
          lastFetchMs_ = ctx().clock.millis();
          notFound_ = f.error == HaError::NotFound;
          error_ = notFound_ ? HaError::None : f.error;
          if (f.parsed) {
            weather_ = f.weather;
            ctx().log("read: %s, %d days, %d hours, %d rain blocks", weather_.now.condition.c_str(),
                      static_cast<int>(weather_.daily.size()), static_cast<int>(weather_.hourly.size()),
                      f.rain ? static_cast<int>(weather_.rain.size()) : -1);
            if (!f.daily || !f.hourly) ctx().toast("Forecast not available");
          }
          showStatus();
          rebuild();
          armTimer();
        });
  }

  std::string entity_, rainEntity_;
  Weather weather_;
  HaError error_ = HaError::None;
  bool notFound_ = false, busy_ = false, fetchedOnce_ = false, timerArmed_ = false;
  uint32_t lastFetchMs_ = 0;
  ui::Column* holder_ = nullptr;
  ui::Label* status_ = nullptr;
  WeatherView* view_ = nullptr;
};

class WeatherApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<WeatherScreen>(); }
};

}  // namespace

POS_REGISTER_APP(WeatherApp, {
    .id = "weather",
    .name = "Weather",
    .icon = Icon::CloudSun,
    .order = 98,  // after the agenda: the tiles of the other apps keep their places
});

}  // namespace pos::weather
