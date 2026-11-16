// The WhatsApp reply screen (docs/apps/inbox.md "Sending", docs/COMPANION.md 7.2) and the API other apps use to open it.
//
//   #include "apps/inbox/compose.h"
//   whatsapp::sendToFavorite(ctx, "Family", "Somdoku 12:34, no mistakes");  // a game's "Send score"
//
// The screen is pushed on top of the calling app's screens (back returns there). It shows the target, a six-line text field
// (900 characters), Paste and Send; Send always asks for confirmation (target and text), then pushes an encrypted reply for the
// phone to the Home Assistant relay. Pushed: a toast and the screen closes; the phone's result arrives later as a toast in the
// inbox. Failed: a toast with the reason and the text stays.
#pragma once

#include <string>

#include "apps/inbox/whatsapp_send.h"
#include "core/app.h"

namespace pos::whatsapp {

// Opens the reply screen for `to`, prefilled with `text` (cleaned, cut to 900 characters).
bool openCompose(AppContext& ctx, const Target& to, const std::string& text = "");
// Reply to the live WhatsApp notification of the chat called `name` (findByName). None (or no phone paired): a toast that says
// so, and false.
bool sendToFavorite(AppContext& ctx, const std::string& name, const std::string& text);

}  // namespace pos::whatsapp
