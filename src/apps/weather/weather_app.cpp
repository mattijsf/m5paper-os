// Weather: the Google Weather integration's entity in Home Assistant (docs/apps/weather.md). Three pages: now (condition,
// temperature, feels like, wind, humidity, rain) with the next hours and days, today and tomorrow in blocks of three hours,
// all days. The saved copy shows first, then the entity, its two forecasts (weather.get_forecasts, daily and hourly) and
// sun.sun are read on the worker. Read only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

#include "apps/common/ha_error_state.h"
#include "apps/weather/weather_model.h"
#include "core/app.h"
#include "core/fs.h"
#include "core/timefmt.h"
#include "ui/inputs.h"
#include "ui/root.h"
#include "ui/text.h"
#include "ui/widgets.h"

namespace pos::weather {

namespace {

using ui::Anchor;
using ui::Type;

constexpr const char* kOldCacheFile = "/data/cache/weather/weather.json";  // before there were places: removed when seen
constexpr uint32_t kRefreshMs = 15 * 60 * 1000;          // read again while the screen stays open
constexpr int64_t kRefreshSec = 15 * 60;                 // a place is read again when its last read is this old
constexpr int kHours = 8, kHourStride = 2;               // 8 columns, two hours apart: the next 16 hours
constexpr int kDays = 5;                                 // on the first page
constexpr int kAllDays = 10;                             // on the Days page
constexpr int kBlockHours = 3, kBlockDays = 2;           // the Hours page: today and tomorrow in blocks of three hours
constexpr int kPages = 3;                                // now, hours, days
constexpr int64_t kOldNowSec = 90 * 60;                  // a saved "now" older than this gives way to the hour's forecast

std::string dayText(int64_t t, int64_t now) {
  const int d = localDay(t);
  if (now > 0 && d == localDay(now)) return "Today";
  if (now > 0 && d == localDay(now) + 1) return "Tomorrow";
  struct tm tmv;
  if (!localTime(t, tmv)) return "";
  return weekdayShort(tmv.tm_wday) + " " + std::to_string(tmv.tm_mday);
}

std::string percent(double v) { return v == kNone ? "" : std::to_string(std::lround(v)) + "%"; }

// "13:34" today, "Wed 13:34" on another day.
std::string whenText(int64_t t, int64_t now) {
  if (t <= 0) return "";
  if (now > 0 && localDay(t) == localDay(now)) return clockText(t);
  return dayText(t, 0) + " " + clockText(t);
}

// The forecast drawn in one widget, one page at a time (docs/apps/weather.md "Screen"): 0 now with the next hours and
// days, 1 today and tomorrow in blocks of three hours, 2 all days.
class WeatherView : public ui::Widget {
 public:
  void set(const Weather& w, int64_t now) {
    w_ = w;
    now_ = now;
    hours_ = nextHours(w.hourly, now, kHours, kHourStride);
    days_ = nextDays(w.daily, now, kAllDays);
    blocks_ = dayBlocks(w.hourly, now, kBlockHours, kBlockDays);
    invalidate();
  }
  void setPage(int page) {
    page_ = page;
    invalidate();
  }
  ui::Size measure(int availW) override { return {availW, 700}; }

  void draw(Canvas& c) override {
    const int x = frame.x, y = frame.y, w = frame.w, bottom = frame.y + frame.h;
    if (page_ == 1) return drawBlocks(c, x, y + 8, w, bottom);
    if (page_ == 2) return drawDays(c, x, y + 8, w, bottom, kAllDays, std::max(56, std::min(64, (bottom - y - 36) / kAllDays)));
    drawNow(c, x, y + 8, w);
    int top = y + 168;
    c.drawFastHLine(x, top, w, ui::divider);
    // The minute forecast, if the place has one and it is fresh: a graph when rain is coming, one line when it is dry.
    RainSummary rain;
    if (now_ > 0 && w_.fetched > 0 && now_ >= w_.fetched && now_ - w_.fetched <= kRainStaleSec) rain = rainSummary(w_.rain, now_);
    if (rain.known) {
      top += drawRain(c, x, top + 8, w, rain) + 16;
      c.drawFastHLine(x, top, w, ui::divider);
    }
    drawHours(c, x, top + 12, w);
    top += 184;
    c.drawFastHLine(x, top, w, ui::divider);
    drawDays(c, x, top + 12, w, bottom, kDays, 60);
  }

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
    ui::drawText(c, ui::ellipsize(c, conditionText(condition), Type::BodyStrong, 276 - 84), x + 84, y + 66, Type::BodyStrong, ui::ink);
    if (old) ui::drawText(c, "Forecast for " + clockText(hour->time), x + 84, y + 100, Type::Body, ui::secondary);
    else if (n.apparent != kNone) ui::drawText(c, "Feels like " + tempText(n.apparent), x + 84, y + 100, Type::Body, ui::ink);
    // Right column: wind, humidity (left out when they are old), rain in the coming hour.
    const int rx = x + 280;  // 168 px of text: "100%  9.9 mm" fits
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
      if (hour->precipitation >= 0.05) rain += "  " + amount(hour->precipitation, n.precipUnit);  // not "0.0 mm"
    }
    line(Icon::Umbrella, rain);
  }

  // The next two hours of the minute forecast. Returns the height used.
  int drawRain(Canvas& c, int x, int y, int w, const RainSummary& rain) {
    if (!rain.wet) {
      ui::drawText(c, rain.headline, x, y + 2, Type::Body, ui::ink);
      return 28;
    }
    ui::drawText(c, rain.headline, x, y, Type::BodyStrong, ui::ink);
    ui::drawText(c, "max " + rateText(rain.peak), x + w, y + 4, Type::Caption, ui::secondary, Anchor::TopRight);
    constexpr int kPlotH = 44;
    const int base = y + 80;
    const int64_t from = now_, to = now_ + kRainWindowSec;
    auto xAt = [&](int64_t t) { return x + static_cast<int>((std::max(from, std::min(t, to)) - from) * w / (to - from)); };
    for (const RainBlock& b : w_.rain) {
      if (b.end <= from || b.start >= to || b.rate < kWetRate) continue;
      const int x0 = xAt(b.start), x1 = xAt(b.end);
      const int h = std::max(3, static_cast<int>(std::lround(rainHeight(b.rate) * kPlotH)));
      c.fillRect(x0 + 1, base - h, std::max(1, x1 - x0 - 2), h, ui::ink);
    }
    c.drawFastHLine(x, base, w, ui::divider);
    // A tick and the time at every half hour.
    for (int64_t t = (from / 1800 + 1) * 1800; t <= to; t += 1800) {
      const int tx = xAt(t);
      c.drawFastVLine(tx, base, 5, ui::divider);
      const std::string text = clockText(t);
      const int half = ui::measureText(c, text, Type::Caption) / 2;
      ui::drawText(c, text, std::max(x + half, std::min(tx, x + w - half)), base + 6, Type::Caption, ui::secondary, Anchor::TopCenter);
    }
    return 108;
  }

  void drawHours(Canvas& c, int x, int y, int w) {
    ui::drawText(c, "Next hours", x, y, Type::Caption, ui::secondary);
    if (hours_.empty()) {
      ui::drawText(c, "No hourly forecast", x, y + 64, Type::Body, ui::ink);
      return;
    }
    const int n = kHours;
    for (int i = 0; i < static_cast<int>(hours_.size()); ++i) {
      const Step& s = hours_[i];
      const int cx = x + (2 * i + 1) * w / (2 * n);
      ui::drawText(c, i == 0 ? "Now" : clockText(s.time), cx, y + 36, Type::Caption, ui::secondary, Anchor::TopCenter);
      ui::drawIcon(c, conditionIcon(s.condition, isNight(s)), cx, y + 88, 32, ui::ink);
      ui::drawText(c, tempText(s.temperature), cx, y + 112, Type::Body, ui::ink, Anchor::TopCenter);
      if (s.precipProbability >= 1) ui::drawText(c, percent(s.precipProbability), cx, y + 144, Type::Caption, ui::ink, Anchor::TopCenter);
    }
  }

  // Today and tomorrow, each a row or two of blocks: a block is three hours, its place in the row is its time of day
  // (00, 03, 06, 09 in the first row, 12, 15, 18, 21 in the second), so a row that is past is left out.
  void drawBlocks(Canvas& c, int x, int y, int w, int bottom) {
    if (blocks_.empty()) {
      ui::drawText(c, "No hourly forecast", x, y + 8, Type::Body, ui::ink);
      return;
    }
    constexpr int kCols = 4, kGap = 8, kHeader = 44, kCellH = 146;
    const int cellW = (w - (kCols - 1) * kGap) / kCols;
    int top = y;
    for (const DayBlocks& day : blocks_) {
      const int firstRow = day.blocks.front().slot / kCols;
      // As many rows of the day as fit (a second day's afternoon can fall off below the place switcher).
      const int rows = std::min(day.blocks.back().slot / kCols - firstRow + 1, (bottom - top - kHeader + kGap) / (kCellH + kGap));
      if (rows <= 0) break;
      ui::drawText(c, dayText(day.time, now_), x, top + kHeader / 2 - 2, Type::BodyStrong, ui::ink, Anchor::MiddleLeft);
      // The sun's next rise and set, on the day they fall on.
      int right = x + w;
      const int today = localDay(day.time);
      auto sun = [&](Icon icon, int64_t t) {
        if (t <= 0 || localDay(t) != today) return;
        const std::string text = clockText(t);
        const int tw = ui::measureText(c, text, Type::Body);
        ui::drawText(c, text, right, top + kHeader / 2 - 2, Type::Body, ui::ink, Anchor::MiddleRight);
        ui::drawIcon(c, icon, right - tw - 24, top + kHeader / 2 - 2, 32, ui::ink);
        right -= tw + 24 + 16 + 20;
      };
      sun(Icon::Sunset, w_.sunset);
      sun(Icon::Sunrise, w_.sunrise);
      top += kHeader;
      for (const Block& b : day.blocks) {
        const int col = b.slot % kCols, row = b.slot / kCols - firstRow;
        if (row >= rows) break;
        const int bx = x + col * (cellW + kGap), by = top + row * (kCellH + kGap), cx = bx + cellW / 2;
        c.drawRoundRect(bx, by, cellW, kCellH, 8, ui::divider);
        char range[16];
        std::snprintf(range, sizeof range, "%02d\xE2\x80\x93%02d", b.fromHour, b.toHour);
        ui::drawText(c, range, cx, by + 8, Type::Caption, ui::secondary, Anchor::TopCenter);
        ui::drawIcon(c, conditionIcon(b.condition, b.night), cx, by + 50, 32, ui::ink);
        ui::drawText(c, tempText(b.temperature), cx, by + 68, Type::BodyStrong, ui::ink, Anchor::TopCenter);
        if (b.precipProbability >= 1) {
          ui::drawText(c, percent(b.precipProbability), cx, by + 97, Type::Caption, ui::ink, Anchor::TopCenter);
          if (b.precipitation >= 0.05)
            ui::drawText(c, amount(b.precipitation, w_.now.precipUnit), cx, by + 119, Type::Caption, ui::ink, Anchor::TopCenter);
        }
      }
      top += rows * (kCellH + kGap);
    }
  }

  void drawDays(Canvas& c, int x, int y, int w, int bottom, int count, int rowH) {
    ui::drawText(c, "Next days", x, y, Type::Caption, ui::secondary);
    if (days_.empty()) {
      ui::drawText(c, "No daily forecast", x, y + 64, Type::Body, ui::ink);
      return;
    }
    const size_t n = std::min(days_.size(), static_cast<size_t>(std::max(0, std::min(count, (bottom - y - 28) / rowH))));
    double lo = 1e9, hi = -1e9;  // the scale of the range bars: the rows shown
    for (size_t i = 0; i < n; ++i) {
      for (double v : {days_[i].templow, days_[i].temperature}) {
        if (v == kNone) continue;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
    }
    const int barX = x + 328, barW = 112;
    int top = y + 28;
    for (size_t i = 0; i < n; ++i, top += rowH) {
      const Step& d = days_[i];
      const int cy = top + rowH / 2;
      ui::drawText(c, dayText(d.time, now_), x, cy, Type::BodyStrong, ui::ink, Anchor::MiddleLeft);
      ui::drawIcon(c, conditionIcon(d.condition), x + 148, cy, 32, ui::ink);
      if (d.precipProbability >= 1) {
        const bool mm = d.precipitation >= 0.05;
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
      if (i + 1 < n) c.drawFastHLine(x, top + rowH, w, ui::divider);
    }
  }

  Weather w_;
  int64_t now_ = 0;
  int page_ = 0;
  std::vector<Step> hours_, days_;
  std::vector<DayBlocks> blocks_;
};

struct Fetched {
  int place = 0;
  HaError error = HaError::None;  // of the entity's state; forecasts that fail keep the saved ones
  bool parsed = false, daily = false, hourly = false;
  Weather weather;
};

class WeatherScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Weather");
    setAction(Icon::RefreshCw, [this] { refresh(); });
    places_ = placesFrom([this](const std::string& key, const std::string& def) {
      if (key == "sun") return std::to_string(ctx().store.get("sun", 1));
      return ctx().store.get(key, def.c_str());
    });
    if (places_.empty()) places_.push_back(Place{"Thuis", "weather.thuis", "", true});
    place_ = std::max(0, std::min(ctx().store.get("place", 0), static_cast<int>(places_.size()) - 1));
    if (fs::exists(kOldCacheFile)) fs::removeAll(kOldCacheFile);
    if (places_.size() > 1) {
      std::vector<std::string> names;
      for (const Place& p : places_) names.push_back(p.name);
      auto* switcher = root.add<ui::Segmented>(names, place_);
      switcher->onChange = [this](int i) { showPlace(i); };
      root.add<ui::Spacer>(ui::s1);
    }
    loadSaved();
    holder_ = root.add<ui::Column>(0);
    holder_->flex = 1;
    status_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    root.add<ui::Spacer>(ui::s1);
    showStatus();
    rebuild();
  }

  void onShow() override {
    if (!tried_ || !fresh()) refresh();
  }

  std::string debugState() const override {
    return std::string(weather_.valid() ? "weather=" + weather_.now.condition : "weather=none") + "|" + status_->text() +
           (error_ != HaError::None ? std::string("|error=") + haErrorText(error_) : "") + (notFound_ ? "|not found" : "") +
           "|page=" + std::to_string(page_ + 1) + "|place=" + place().name;
  }

 private:
  const Place& place() const { return places_[static_cast<size_t>(place_)]; }

  void loadSaved() {
    weather_ = Weather{};
    std::string cached;
    if (fs::readFile(cacheFile(place().entity), cached)) fromJson(cached, weather_);
  }

  // The other place: its saved copy at once, then a read if that copy is old.
  void showPlace(int i) {
    if (i == place_ || i < 0 || i >= static_cast<int>(places_.size())) return;
    place_ = i;
    ctx().store.set("place", i);
    ctx().store.save();
    error_ = HaError::None;
    notFound_ = false;
    loadSaved();
    showStatus();
    rebuild();
    if (!fresh()) refresh();
  }

  // The copy of the place shown was read less than 15 minutes ago.
  bool fresh() const {
    const int64_t now = ctx().clock.now();
    return weather_.valid() && now > 0 && weather_.fetched > 0 && now >= weather_.fetched && now - weather_.fetched < kRefreshSec;
  }

  void rebuild() {
    holder_->removeAll();
    view_ = nullptr;
    if (weather_.valid()) {
      auto* pager = holder_->add<ui::Pager>(kPages, page_);
      pager->flex = 1;
      view_ = pager->add<WeatherView>();
      view_->set(weather_, ctx().clock.now());
      view_->setPage(page_);
      pager->onPage = [this](int p) {
        page_ = p;
        view_->setPage(p);
      };
    } else if (notFound_) {
      holder_->add<ui::EmptyState>(Icon::Cloud, "No weather in Home Assistant", place().entity + " was not found or has no data.", "Retry",
                                   [this] { refresh(); })->flex = 1;
    } else if (error_ != HaError::None) {
      apps::addHaErrorState(*holder_, error_, [this] { refresh(); });
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
    const std::string entity = place().entity, rainEntity = place().rain;
    const bool withSun = place().sun;
    const int index = place_;
    const Weather old = weather_;
    const int64_t now = ctx().clock.now();
    ctx().async(
        [ha, entity, rainEntity, withSun, index, old, now]() mutable {
          Fetched f;
          f.place = index;
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
          // The sun's next rise and set (sun.sun: Home Assistant's own location, so only for the place that is there);
          // without an answer the saved times stay.
          if (withSun) {
            HaResult sun = ha.get("/api/states/sun.sun");
            if (sun.ok()) parseSun(sun.body, f.weather.sunrise, f.weather.sunset);
          } else {
            f.weather.sunrise = f.weather.sunset = 0;
          }
          // The minute forecast of the place, if it has one: only a fresh one is kept, and only what is still ahead.
          f.weather.rain.clear();
          if (!rainEntity.empty()) {
            HaResult rain = ha.get("/api/states/" + rainEntity);
            std::vector<RainBlock> blocks;
            int64_t reported = 0;
            if (rain.ok() && parseRain(rain.body, blocks, reported) && (reported <= 0 || now <= 0 || now - reported <= kRainStaleSec)) {
              for (const RainBlock& b : blocks)
                if (b.end > now && b.start < now + 6 * 3600 && f.weather.rain.size() < 96) f.weather.rain.push_back(b);
            }
          }
          fs::writeFile(cacheFile(entity), toJson(f.weather));
          return f;
        },
        [this](Fetched f) {
          busy_ = false;
          if (f.place != place_) {  // the place was changed while this read ran: its copy is saved, the new place is read now
            if (!fresh()) refresh();
            armTimer();  // (a fresh copy of the new place starts no read: the regular refresh must go on all the same)
            return;
          }
          tried_ = true;
          notFound_ = f.error == HaError::NotFound;
          error_ = notFound_ ? HaError::None : f.error;
          if (f.parsed) {
            weather_ = f.weather;
            ctx().log("read: %s, %d days, %d hours, %d rain blocks", weather_.now.condition.c_str(), static_cast<int>(weather_.daily.size()),
                      static_cast<int>(weather_.hourly.size()), static_cast<int>(weather_.rain.size()));
            if (!f.daily || !f.hourly) ctx().toast("Forecast not available");
          }
          showStatus();
          rebuild();
          armTimer();
        });
  }

  std::vector<Place> places_;
  int place_ = 0;
  Weather weather_;  // of the place shown
  HaError error_ = HaError::None;
  bool notFound_ = false, busy_ = false, timerArmed_ = false;
  bool tried_ = false;  // a read ended since the app was opened
  int page_ = 0;  // kept across a read: the view is built again
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
