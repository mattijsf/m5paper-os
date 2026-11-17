#include "apps/common/daily_game.h"

#include "core/clipboard.h"

namespace pos::apps {

bool today(hal::Clock& clock, Date& date, struct tm* tmOut) {
  const int64_t now = clock.now();
  struct tm t{};
  if (!clockIsSet(now) || !localTime(now, t)) return false;
  date = Date{t.tm_year + 1900, t.tm_mon + 1, t.tm_mday};
  if (tmOut) *tmOut = t;
  return true;
}

void ClockSetter::start(AppContext& ctx, std::function<void()> done) {
  if (busy_) return;
  busy_ = true;
  error_.clear();
  AppContext* c = &ctx;
  ctx.syncClock([this, c, done = std::move(done)](bool ok, const std::string& error) {
    busy_ = false;
    if (!ok) {
      c->log("clock not set: %s", error.c_str());
      error_ = error == AppContext::kNoNetworkTime ? "Wel wifi, maar geen tijd ontvangen. Probeer het nog eens."
                                                   : "Geen wifi. Probeer het nog eens.";
    }
    if (done) done();
  });
}

std::string ClockSetter::hint(const std::string& idle) const {
  if (busy_) return "Verbinden met wifi...";
  return error_.empty() ? idle : error_;
}

void copyScore(AppContext& ctx, const std::string& score) {
  ctx.toast(clipboard::set(score, ctx.appId()) ? "Score gekopieerd" : "Kopi\xC3\xABren lukte niet");
}

std::string daysText(int days) { return std::to_string(days) + (days == 1 ? " dag" : " dagen"); }

}  // namespace pos::apps
