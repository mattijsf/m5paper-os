// The WhatsApp compose screen (docs/apps/inbox.md "Sending", DECISIONS.md D21) and the API other apps use to open it.
//
//   #include "apps/inbox/compose.h"
//   whatsapp::sendToFavorite(ctx, "Family", "Somdoku 12:34, no mistakes");  // a game's "Send score"
//
// The screen is pushed on top of the calling app's screens (back returns there). It shows the target, a six-line text field
// (1000 characters), Paste and Send; Send always asks for confirmation (target and text) before it calls the script. Sent: a
// toast and the screen closes. Refused or failed: a toast with the reason and the text stays.
#pragma once

#include <string>

#include "apps/inbox/whatsapp_send.h"
#include "core/app.h"

namespace pos::whatsapp {

// Opens the compose screen for `to`, prefilled with `text` (cleaned, cut to 1000 characters). False, with a toast, if `to.chat`
// is not a valid chat JID.
bool openCompose(AppContext& ctx, const Target& to, const std::string& text = "");
// "New message": the favourites of the config (inbox.whatsapp.favorites). None: a toast. One: straight to compose. More: a
// list to pick from first.
void openNewMessage(AppContext& ctx, const std::string& text = "");
// Compose to the favourite called `name`; if there is none by that name, openNewMessage. True if the favourite was found.
bool sendToFavorite(AppContext& ctx, const std::string& name, const std::string& text);

}  // namespace pos::whatsapp
