// Sending a WhatsApp message from the device (DECISIONS.md D21, docs/BACKEND.md "Sending a WhatsApp message", docs/apps/inbox.md
// "Sending"). The device calls Home Assistant's script.paperos_whatsapp_send, which holds the guards (length, chat form, rate limit);
// these checks only keep the device from asking for what the script would refuse. Pure logic, unit-tested; the screens are in compose.h.
#pragma once

#include <string>
#include <vector>

#include "core/config.h"
#include "core/ha.h"

namespace pos::whatsapp {

constexpr size_t kMaxChars = 1000;  // the script's limit, in characters after trimming
constexpr size_t kMaxNameChars = 40;

// A chat a message can go to: a name for the screen and the chat JID.
struct Target {
  std::string name;
  std::string chat;
};

// <digits>@s.whatsapp.net (a person) or <digits>@g.us / <digits>-<digits>@g.us (a group): the same rule as the script.
bool validChat(const std::string& jid);
bool isGroup(const std::string& jid);
// The usable favourites of the config (inbox.whatsapp.favorites): valid JIDs only, names cut to kMaxNameChars characters.
std::vector<Target> favorites(const Config& config);
// Spaces, tabs and line feeds removed at both ends (what the script's `trim` does to a message from the device).
std::string trimmed(const std::string& text);
// The service data of the script call: {"chat":"...","text":"..."} with the text trimmed.
std::string sendBody(const Target& to, const std::string& text);

// What became of a send, from the answer of POST /api/services/script/paperos_whatsapp_send?return_response.
struct SendOutcome {
  bool sent = false;
  std::string reason;   // the script's reason (empty, too_long, bad_chat, too_soon, hourly_limit, send_failed), "" otherwise
  std::string message;  // one line for a toast: "Sent to Family", "Not sent: wait 10 seconds", ...
};
SendOutcome sendOutcome(const HaResult& r, const std::string& toName);
// The toast line for a refusal reason of the script.
std::string reasonText(const std::string& reason);

}  // namespace pos::whatsapp
