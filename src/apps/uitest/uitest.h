// Widget test screens (simulator `--app widgets1`, ...): every widget in each state, used by the
// UI scripts in tests/ui/. Not an app the user sees.
#pragma once

#include <memory>
#include <string>

#include "ui/screen.h"

namespace pos::apps::uitest {

// nullptr if `name` is not a test screen.
std::unique_ptr<ui::Screen> create(const std::string& name, Canvas& canvas, DisplayManager& dm);

}  // namespace pos::apps::uitest
