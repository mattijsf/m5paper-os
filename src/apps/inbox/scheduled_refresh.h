// Scheduled refresh (task P7-5, docs/POWER.md): the RTC woke the switched-off device. Fetch the inbox feeds, update the launcher badges
// and the sleep screen's summary line, and switch off again until the next alarm. Nothing else runs: no launcher, no app.
#pragma once

#include <memory>

#include "core/ha.h"
#include "core/os.h"

namespace pos::inbox {

std::unique_ptr<Surface> createRefreshSurface(Os& os);

// What one refresh found: total unread over the feeds that answered, and whether any did.
struct RefreshResult {
  int unread = 0;
  int feedsOk = 0;
};
// Reads the companion relay (relay_fetch.h) and writes both inbox badges. Blocking (worker thread).
RefreshResult refreshFeeds(HaClient ha, int64_t now, bool takeClip);

}  // namespace pos::inbox
