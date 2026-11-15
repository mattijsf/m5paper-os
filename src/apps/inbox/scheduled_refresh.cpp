#include "apps/inbox/scheduled_refresh.h"

#include <cstdio>
#include <ctime>

#include "apps/apps.h"
#include "apps/inbox/inbox_model.h"
#include "core/fs.h"
#include "core/ha.h"
#include "core/log.h"
#include "core/sleep_screen.h"
#include "core/store.h"
#include "core/timefmt.h"
#include "ui/image.h"

namespace pos::inbox {

namespace {

// The feeds of the two inbox apps (the same ids and sensors as apps/inbox/inbox_app.cpp).
struct FeedDef {
  const char* id;
  const char* entity;
};
constexpr FeedDef kFeeds[] = {{"whatsapp", "sensor.paperos_whatsapp_inbox"}, {"mail", "sensor.paperos_mail_inbox"}};
constexpr uint32_t kMaxAwakeMs = 45 * 1000;  // whatever happens, the device is off again after this

class RefreshSurface : public Surface {
 public:
  explicit RefreshSurface(Os& os) : os_(os), inputAtStart_(os.lastInputMs()) {}
  ~RefreshSurface() override {
    if (timer_) os_.timers().cancel(timer_);
    *alive_ = false;
  }

  // A touch means somebody is there (the side button was pressed right at the alarm time): end in an ordinary boot, not off.
  // Any touch event counts, also a finger that is still down (see touched()).
  void onGesture(const Gesture&) override {}
  bool dirty() const override { return !started_; }
  bool keepsPanelAtBoot() const override { return true; }  // the sleep screen of the last power-off is still on the panel
  void render(Canvas& c, DisplayManager&) override {
    if (started_) return;
    started_ = true;
    c.fillScreen(kWhite);  // nothing is pushed until the feeds answered
    t0_ = os_.platform().clock.millis();
    POS_LOG("sched", "scheduled refresh: fetching the inbox feeds");
    HaClient ha = HaClient::fromNet(os_.net(), os_.secrets());
    auto alive = alive_;
    os_.worker().async([ha]() mutable { return refreshFeeds(ha); }, [this, alive](RefreshResult r) {
      if (*alive) finish(r);
    });
    timer_ = os_.timers().after(kMaxAwakeMs, [this] {
      timer_ = 0;
      finish(RefreshResult{});
    });
  }

 private:
  bool touched() const { return os_.lastInputMs() != inputAtStart_ || os_.touching(); }
  void finish(const RefreshResult& r) {
    if (finished_) return;
    finished_ = true;
    const bool touched_ = touched();
    hal::Platform& p = os_.platform();
    // The same sleep screen as at power-off, with the new summary.
    SleepInfo info;
    const bool have = loadSleepInfo(info);
    std::shared_ptr<Canvas> image;
    if (have && !info.image.empty()) image = ui::loadImageFit(info.image, kSleepImageW, kSleepImageH);
    std::string updated;
    const int64_t now = p.clock.now();
    if (r.feedsOk > 0 && now > 0) {
      struct tm tmv;
      localTime(now, tmv);
      char buf[8];
      std::snprintf(buf, sizeof buf, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
      updated = buf;
    }
    if (r.feedsOk > 0) {  // nothing answered: the panel keeps what it shows (no flash, less time awake)
      drawSleepScreen(os_.canvas(), have ? &info : nullptr, image.get(), sleepNote(cachedUnread(), updated));
      os_.displayManager().forceClean();
    }
    os_.net().disconnect();
    // Two times: since the boot (the clock starts in setup(), the ROM and bootloader come before it) and the refresh alone.
    const uint32_t sinceBoot = p.clock.millis(), took = sinceBoot - t0_;
    POS_LOG("sched", "scheduled refresh done: %d feed(s), %d unread, %u ms (%u ms since boot)", r.feedsOk, r.unread,
            static_cast<unsigned>(took), static_cast<unsigned>(sinceBoot));
    // /data/refresh.log: unix,msSinceBoot,refreshMs,feedsAnswered,unread  (tools/device.py cat /data/refresh.log)
    fs::appendFile("/data/refresh.log", std::to_string(now) + "," + std::to_string(sinceBoot) + "," + std::to_string(took) + "," +
                                            std::to_string(r.feedsOk) + "," + std::to_string(r.unread) + "\n");
    if (os_.offAgainAfterRefresh() > 0 && !touched_) {  // a test (>poweroff N M): off once more, the next wake stays on
      os_.powerOffForSleep(os_.offAgainAfterRefresh(), true);
      return;
    }
    if (os_.stayOnAfterRefresh() || touched_) {  // a test (>poweroff N) or a touch: an ordinary boot instead of switching off again
      os_.requestSurface(apps::createSurface("", os_, true));
      return;
    }
    os_.powerOffForSleep();  // the next alarm, or off until the side button if the setting was switched off meanwhile
  }

  Os& os_;
  bool started_ = false, finished_ = false;
  uint32_t inputAtStart_;
  uint32_t t0_ = 0;
  Timers::Id timer_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

}  // namespace

RefreshResult refreshFeeds(HaClient ha) {
  RefreshResult out;
  for (const FeedDef& f : kFeeds) {
    HaResult r = ha.get(std::string("/api/states/") + f.entity);
    Feed feed;
    if (!r.ok() || !parseFeed(r.body, feed)) continue;
    ++out.feedsOk;
    Store app(std::string("/data/apps/") + f.id + ".json");  // the app's own store: its seen marker
    const int n = newCount(feed.items, app.get("seen", 0));
    out.unread += n;
    fs::writeFile(std::string("/data/cache/inbox/") + f.id + ".json", r.body);
    fs::writeFile(std::string("/data/cache/") + f.id + "/badge", std::to_string(n));
  }
  return out;
}

std::unique_ptr<Surface> createRefreshSurface(Os& os) { return std::make_unique<RefreshSurface>(os); }

}  // namespace pos::inbox
