// The screen an app shows when Home Assistant could not be read and it has nothing saved to show instead: one wording
// for every app (Weather, the inbox, Agenda), in the user's terms (docs/APPS.md "Errors").
#pragma once

#include <functional>

#include "core/ha.h"
#include "ui/panels.h"

namespace pos::apps {

// Adds the EmptyState for `error` to `holder` (it takes the leftover height) and returns it.
//   no address or token on the card   "Not set up yet", where to put them (no Retry: trying again cannot help)
//   the token refused (401 / 403)     "Home Assistant refused the token", Retry
//   anything else                     "Cannot reach Home Assistant" with what went wrong, Retry
ui::EmptyState* addHaErrorState(ui::Widget& holder, HaError error, std::function<void()> retry);

}  // namespace pos::apps
