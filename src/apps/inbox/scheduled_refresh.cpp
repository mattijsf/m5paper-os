#include "apps/inbox/scheduled_refresh.h"

#include <cstdio>
#include <ctime>

#include "apps/apps.h"
#include "apps/inbox/inbox_model.h"
#include "apps/inbox/relay_fetch.h"
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
constexpr FeedDef kFeeds[] = {{"whatsapp", companion::kToPaperEntity}, {"mail", companion::kToPaperEntity}};
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
    const int64_t now = os_.platform().clock.now();
    const bool clip = os_.config().phone.shareClipboard;
    os_.worker().async([ha, now, clip]() mutable { return refreshFeeds(ha, now, clip); }, [this, alive](RefreshResult r) {
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
    const int64_t now = p.clock.now();
    const std::string updated = r.feedsOk > 0 ? clockText(now) : std::string();
    if (r.feedsOk > 0) {  // nothing answered: the panel keeps what it shows (no flash, less time awake)
      drawSleepScreen(os_.canvas(), have ? &info : nullptr, image.get(), sleepNote(cachedUnread(), updated), sleepAgendaLine(now));
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

int writeBadges(const companion::Notifs& notifs, bool gone) {
  int unread = 0;
  for (const FeedDef& d : kFeeds) {
    const Feed feed = feedFromNotifs(notifs, std::string(d.id) == "mail");
    Store app(std::string("/data/apps/") + d.id + ".json");  // the app's own store: its seen marker
    const int n = gone ? 0 : newCount(feed.items, app.get("seen", 0));
    unread += n;
    fs::mkdirs(std::string("/data/cache/") + d.id);
    fs::writeFile(std::string("/data/cache/") + d.id + "/badge", std::to_string(n));
  }
  return unread;
}

RefreshResult refreshFeeds(HaClient ha, int64_t now, bool takeClip) {
  RefreshResult out;
  RelayFetch f = fetchRelay(ha, now, takeClip);  // one queue for both apps (docs/COMPANION.md)
  const bool ok = f.paired && f.http.ok() && f.applied.parsed;
  const bool gone = !f.paired || f.http.error == HaError::NotFound;  // no badge for a feed that cannot exist
  if (!ok && !gone) return out;  // an error: keep the badges as they were
  out.unread = writeBadges(f.notifs, gone);
  if (ok) out.feedsOk = static_cast<int>(sizeof kFeeds / sizeof kFeeds[0]);
  return out;
}

std::unique_ptr<Surface> createRefreshSurface(Os& os) { return std::make_unique<RefreshSurface>(os); }

}  // namespace pos::inbox
