// Composition root for the shared code: which Surface the Os shows. Platform mains call this.
#pragma once

#include <memory>
#include <string>

#include "core/os.h"

namespace pos::apps {

// "" -> the Shell with the launcher; a registered app id -> the Shell with that app open;
// a UI test screen name (apps/uitest) -> that screen; anything else (or "demo") -> nullptr (the Os demo).
// `resume` (the boot path): with name "" the app that was open at the last power-off is opened again.
std::unique_ptr<Surface> createSurface(const std::string& name, Os& os, bool resume = false);

}  // namespace pos::apps
