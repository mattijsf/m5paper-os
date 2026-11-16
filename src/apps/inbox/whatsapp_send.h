// Replying to a WhatsApp message from the device (docs/COMPANION.md 7.2 `reply`, docs/apps/inbox.md "Sending"). Since D22 a
// reply is an encrypted envelope for the phone, pushed to the Home Assistant relay; the phone app sends it through the
// notification's reply action and holds the guards (length, 10 s, 20 per hour). Only chats with a live notification that has a
// reply action can be answered. Pure logic, unit-tested; the screens are in compose.h.
#pragma once

#include <string>

#include "core/companion.h"
#include "core/ha.h"

namespace pos::whatsapp {

constexpr size_t kMaxChars = companion::kMaxReplyChars;  // 900 characters after trimming
constexpr size_t kMaxNameChars = 40;

// A chat a reply can go to: a name for the screen and the phone's notification key.
struct Target {
  std::string name;
  std::string key;
};

// The name of a notification's chat: the group or conversation title, else the latest sender.
std::string chatName(const companion::Notif& n);
// The reply target for a notification key: false unless it is a WhatsApp notification with a reply action.
bool replyTarget(const companion::Notifs& notifs, const std::string& key, Target& out);
// A WhatsApp notification with a reply action whose chat name is `name` (a game's "send score"); the newest wins.
bool findByName(const companion::Notifs& notifs, const std::string& name, Target& out);
// Spaces, tabs and line feeds removed at both ends.
std::string trimmed(const std::string& text);

// What became of the push of a reply to the relay (script.paperos_relay_push).
struct SendOutcome {
  bool sent = false;    // in the relay: the phone sends it when it reads its queue
  std::string message;  // one line for a toast
};
SendOutcome pushOutcome(const HaResult& r);

}  // namespace pos::whatsapp
