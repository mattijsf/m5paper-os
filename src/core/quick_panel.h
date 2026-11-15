// The quick panel (docs/apps/launcher.md): a sheet that a swipe down from the status bar opens, on every screen that has one.
// Wi-Fi state, battery detail, "Sleep now" and "Clean screen".
#pragma once

namespace pos {

class Shell;

// Opens the panel over the Shell's screen (no-op if a sheet is already open or the sleep screen is showing).
void openQuickPanel(Shell& shell);

}  // namespace pos
