// The launcher: the home screen at the base of the Shell's stack (docs/apps/launcher.md).
#pragma once

#include <string>

#include "core/shell.h"

namespace pos::apps::launcher {

void install(Shell& shell);

// "3 unread - Reading: Dune, 42%": the header's summary line. Parts that are not there are left out; "" if there is nothing to say.
// `unread` is the sum of the apps' cached badges, `title` / `percent` the reader's last book (percent < 0: not shown).
std::string summaryLine(int unread, const std::string& title, int percent);

}  // namespace pos::apps::launcher
