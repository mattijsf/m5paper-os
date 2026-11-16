// The clipboard shared with the paired phone (DECISIONS.md D23, docs/COMPANION.md 7 `clip`). Installs clipboard::Sync:
//   - push: every copy on the Paper (Copy all, a game's score) goes to the phone as an encrypted `clip` (silent); the push joins
//     Wi-Fi itself. If it fails for want of Wi-Fi (or a timeout, a server error) the copy is resent when Wi-Fi next comes up,
//     whatever brought it up (net onConnected), within 24 hours of the copy
//   - pull: a text field's clipboard sheet and the reply screen's Paste read the relay first, so what the phone shared is there
// Only while a phone is paired and Settings > Home Assistant > Phone shares the clipboard (config phone.clipboard).
#pragma once

#include "core/os.h"

namespace pos::inbox {

// Once at boot, after the config is loaded. The Os outlives every use.
void installClipboardSync(Os& os);

}  // namespace pos::inbox
