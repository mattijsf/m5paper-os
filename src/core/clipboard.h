// The OS clipboard (DECISIONS.md D21): one piece of plain text, shared by all apps. A game copies a score, the WhatsApp compose
// screen pastes it; every ui::TextField offers Paste / Copy all / Clear on a long press.
//   - text only, UTF-8, at most kMaxBytes (cut at a character boundary, never in the middle of one)
//   - kept in /data/clipboard.txt (first line: who copied it, then the text), so it survives a power-off
//   - never logged: it may hold a message or a password typed into a field. Masked fields do not offer Copy.
//   - shared with the paired phone (DECISIONS.md D23, COMPANION.md 7 `clip`) through the Sync hooks below
// UI thread, except set(text, kFromPhone), which the relay fetch calls on the worker (the file is replaced atomically).
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace pos::clipboard {

constexpr size_t kMaxBytes = 4096;
constexpr const char* kFromPhone = "phone";  // the source of a clipboard that came from the phone: never sent back

// Replaces the clipboard. `source` says who copied it (an app id, "field"): one short word, shown nowhere yet.
// The text is cleaned first (see clean()); an empty result clears the clipboard. Returns false if the file could not be written.
bool set(const std::string& text, const std::string& source = "");
// The text, "" if the clipboard is empty.
std::string get();
// Who copied it, "" if unknown or empty.
std::string source();
bool empty();
void clear();

// Sync with the paired phone. Installed once at boot (apps/inbox/clipboard_sync.h); without it the clipboard stays local.
struct Sync {
  std::function<bool()> active;                            // a phone is paired and Settings shares the clipboard
  std::function<void(const std::string& text)> push;       // after set() by anything but the phone: send it (best effort)
  std::function<void(std::function<void(bool)>)> pull;     // read the relay; done(true) if the clipboard changed (UI thread)
};
void setSync(Sync s);
// True while a phone takes part (Sync::active).
bool syncing();
// Asks the relay for a newer clipboard from the phone, then calls `done(changed)` on the UI thread; at once with false when
// not syncing.
void pull(std::function<void(bool changed)> done);

// What set() stores: invalid UTF-8 and control characters dropped (line feeds stay, CR LF becomes LF, a tab becomes a space),
// then cut to `maxBytes` at a character boundary.
std::string clean(const std::string& text, size_t maxBytes = kMaxBytes);
// The longest prefix of valid UTF-8 `s` that is at most `maxBytes` long and ends at a character boundary.
std::string truncateUtf8(const std::string& s, size_t maxBytes);

}  // namespace pos::clipboard
