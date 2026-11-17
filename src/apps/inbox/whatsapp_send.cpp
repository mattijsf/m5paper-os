#include "apps/inbox/whatsapp_send.h"

#include "core/strings.h"
#include "core/utf8.h"

namespace pos::whatsapp {

std::string chatName(const companion::Notif& n) {
  std::string name = !n.chat.empty() ? n.chat : (n.msgs.empty() ? std::string() : n.msgs.back().from);
  for (char& c : name)
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
  name = trimmed(name);
  return utf8::prefix(name.empty() ? std::string("WhatsApp chat") : name, kMaxNameChars);
}

bool replyTarget(const companion::Notifs& notifs, const std::string& key, Target& out) {
  const companion::Notif* n = companion::findNotif(notifs, key);
  if (!n || n->app != "whatsapp" || !n->reply) return false;
  out = Target{chatName(*n), n->key};
  return true;
}

bool findByName(const companion::Notifs& notifs, const std::string& name, Target& out) {
  const companion::Notif* best = nullptr;
  for (const companion::Notif& n : notifs.items)
    if (n.app == "whatsapp" && n.reply && chatName(n) == utf8::prefix(trimmed(name), kMaxNameChars) && (!best || n.ts > best->ts)) best = &n;
  if (!best) return false;
  out = Target{chatName(*best), best->key};
  return true;
}

SendOutcome pushOutcome(const HaResult& r) {
  SendOutcome o;
  if (r.ok()) {
    o.sent = true;
    o.message = "Sent to your phone";
  } else if (r.error == HaError::Timeout) {
    o.message = "No answer: it may still reach your phone";
  } else if (r.status == 400 || r.status == 404) {
    o.message = "Not sent: no relay in Home Assistant";
  } else {
    o.message = std::string("Not sent: ") + haErrorText(r.error);
  }
  return o;
}

}  // namespace pos::whatsapp
