// Widget test screens (simulator `--app widgets1`, ...): every widget in each state, used by the
// UI scripts in tests/ui/. Not an app the user sees.
#pragma once

#include <memory>
#include <string>

#include "core/os.h"
#include "ui/root.h"

namespace pos::apps::uitest {

// nullptr if `name` is not a test screen.
std::unique_ptr<ui::Root> create(const std::string& name, Os& os);

}  // namespace pos::apps::uitest
