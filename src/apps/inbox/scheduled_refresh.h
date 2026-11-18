// Scheduled refresh (tasks P7-5 and P7-16, docs/POWER.md): the RTC woke the switched-off device. Read the relay (inbox feeds,
// agenda) and the weather, update the launcher badges and the sleep screen, and switch off again until the next alarm.
// Nothing else runs: no launcher, no app.
#pragma once

#include <memory>

#include "core/companion.h"
#include "core/ha.h"
#include "core/os.h"

namespace pos::inbox {

std::unique_ptr<Surface> createRefreshSurface(Os& os);

// What one refresh found: total unread over the feeds that answered, and whether any did.
struct RefreshResult {
  int unread = 0;
  int feedsOk = 0;
  bool weather = false;  // the weather's first place answered (its saved copy is new)
};
// Writes both inbox badges (/data/cache/<id>/badge) from the notification store: what is newer than each app's seen marker.
// `gone`: no feed can exist (not paired, no relay), the badges become 0. Returns the unread total. For every reader of the
// relay queue that is not an inbox app (the scheduled refresh, the Agenda app).
int writeBadges(const companion::Notifs& notifs, bool gone = false);
// The messages a sound is for: unread (newer than their app's seen marker) and newer than `notifs.heard`.
int freshMessages(const companion::Notifs& notifs);
// Reads the companion relay (relay_fetch.h) and writes both inbox badges. Blocking (worker thread).
RefreshResult refreshFeeds(HaClient ha, int64_t now, bool takeClip);
// The scheduled wake's whole read: the relay, then the weather for the sleep screen's board (`withWeather`: config
// power.sleepBoard). The weather is not asked when the relay's request found no network: one Wi-Fi attempt a wake.
RefreshResult refreshForSleep(HaClient ha, int64_t now, bool takeClip, bool withWeather);

}  // namespace pos::inbox
