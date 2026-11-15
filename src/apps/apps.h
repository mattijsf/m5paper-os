// Composition root for the shared code: which Surface the Os shows. Platform mains call this.
#pragma once

#include <memory>
#include <string>

#include "core/os.h"

namespace pos::apps {

// "" -> the Shell with the launcher; a registered app id -> the Shell with that app open;
// a UI test screen name (apps/uitest) -> that screen; anything else (or "demo") -> nullptr (the Os demo).
std::unique_ptr<Surface> createSurface(const std::string& name, Os& os);

}  // namespace pos::apps
