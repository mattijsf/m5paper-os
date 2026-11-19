#include "apps/apps.h"

#include "apps/inbox/clipboard_sync.h"
#include "apps/inbox/scheduled_refresh.h"
#include "apps/launcher/launcher.h"
#include "apps/uitest/uitest.h"
#include "apps/weather/weather_fetch.h"
#include "core/shell.h"
#include "core/sleep_screen.h"
#include "core/store.h"

namespace pos::apps {

namespace {

// The network read of a sleep screen that stays on (core/sleep_screen.h, Shell::sleepScreenTick): what a scheduled wake
// reads, the relay, the weather and links to read later.
SleepReadJob makeSleepRead(Os& os) {
  HaClient ha = HaClient::fromNet(os.net(), os.secrets());
  const int64_t now = os.platform().clock.now();
  const bool clip = os.config().phone.shareClipboard, board = os.config().power.sleepBoard;
  NetService* net = &os.net();
  const reader::Transport send = [net](const hal::HttpRequest& r) { return net->perform(r); };
  return [ha, now, clip, board, send]() mutable {
    const inbox::RefreshResult r = inbox::refreshForSleep(ha, now, clip, board, send, 25000);
    return r.feedsOk > 0 || r.weather || r.articles > 0;
  };
}

void installHooks() {
  setSleepWeatherSource(weather::sleepBoardSource);  // the Today board's weather (core does not include apps); idempotent
  setSleepReadMaker(makeSleepRead);
}

}  // namespace

std::unique_ptr<Surface> createSurface(const std::string& name, Os& os, bool resume) {
  installHooks();
  if (name.empty() || AppRegistry::instance().find(name)) {
    auto shell = std::make_unique<Shell>(os);
    launcher::install(*shell);
    if (!name.empty()) {
      shell->requestOpen(name);
    } else if (resume) {
      // After a power-off the app that was open comes back (its own state is restored by the app). Once: a boot that follows
      // a crash or a reset shows the launcher.
      Store sys("/data/system.json");
      const std::string last = sys.get("lastApp", std::string());
      if (!last.empty()) {
        sys.set("lastApp", std::string());
        sys.save();
        if (AppRegistry::instance().find(last)) shell->requestOpen(last);
      }
    }
    return shell;
  }
  if (auto ui = uitest::create(name, os)) return ui;
  return nullptr;
}

std::unique_ptr<Surface> createBootSurface(const std::string& name, Os& os) {
  inbox::installClipboardSync(os);  // the clipboard shared with a paired phone (D23); idempotent
  const bool scheduled = os.takeScheduledWake();  // always asked: it also forgets the remembered wake time
  if (scheduled && name.empty()) {
    installHooks();
    return inbox::createRefreshSurface(os);
  }
  return createSurface(name, os, true);
}

}  // namespace pos::apps
