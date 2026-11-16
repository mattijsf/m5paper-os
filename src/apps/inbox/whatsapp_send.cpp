#include "apps/inbox/whatsapp_send.h"

#include <ArduinoJson.h>

#include "core/clipboard.h"

namespace pos::whatsapp {

namespace {

bool allDigits(const std::string& s, size_t from, size_t to, size_t minLen, size_t maxLen) {
  if (to < from || to - from < minLen || to - from > maxLen) return false;
  for (size_t i = from; i < to; ++i)
    if (s[i] < '0' || s[i] > '9') return false;
  return true;
}

bool endsWith(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

// The first `n` characters (code points) of valid UTF-8.
std::string firstChars(const std::string& s, size_t n) {
  size_t i = 0;
  while (i < s.size()) {
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
      if (n == 0) break;
      --n;
    }
    ++i;
  }
  return s.substr(0, i);
}

}  // namespace

bool validChat(const std::string& jid) {
  const size_t at = jid.find('@');
  if (at == std::string::npos) return false;
  const std::string domain = jid.substr(at + 1);
  if (domain == "s.whatsapp.net") return allDigits(jid, 0, at, 5, 24);
  if (domain != "g.us") return false;
  const size_t dash = jid.find('-');
  if (dash == std::string::npos || dash > at) return allDigits(jid, 0, at, 5, 24);
  return allDigits(jid, 0, dash, 5, 24) && allDigits(jid, dash + 1, at, 5, 12);  // old-style group id: creator-time
}

bool isGroup(const std::string& jid) { return endsWith(jid, "@g.us"); }

std::vector<Target> favorites(const Config& config) {
  std::vector<Target> out;
  for (const ChatFavorite& f : config.inbox.whatsappFavorites) {
    if (!validChat(f.chat)) continue;
    const std::string name = trimmed(clipboard::clean(f.name, 256));
    if (name.empty()) continue;
    out.push_back(Target{firstChars(name, kMaxNameChars), f.chat});
  }
  return out;
}

std::string trimmed(const std::string& text) {
  const char* ws = " \t\n\r\v\f";
  const size_t b = text.find_first_not_of(ws);
  if (b == std::string::npos) return "";
  return text.substr(b, text.find_last_not_of(ws) - b + 1);
}

std::string sendBody(const Target& to, const std::string& text) {
  JsonDocument doc;
  doc["chat"] = to.chat;
  doc["text"] = trimmed(text);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string reasonText(const std::string& reason) {
  if (reason == "empty") return "Not sent: the message is empty";
  if (reason == "too_long") return "Not sent: over 1000 characters";
  if (reason == "bad_chat") return "Not sent: not a WhatsApp chat";
  if (reason == "too_soon") return "Not sent: wait 10 seconds";
  if (reason == "hourly_limit") return "Not sent: 20 an hour at most";
  if (reason == "send_failed") return "Not sent: WhatsApp did not take it";
  return "Not sent";
}

SendOutcome sendOutcome(const HaResult& r, const std::string& toName) {
  SendOutcome o;
  if (!r.ok()) {
    if (r.error == HaError::Timeout) o.message = "No answer: check WhatsApp first";  // it may have gone out
    else if (r.status == 400 || r.status == 404) o.message = "Not sent: no send script in Home Assistant";
    else o.message = std::string("Not sent: ") + haErrorText(r.error);
    return o;
  }
  JsonDocument doc;
  JsonVariantConst resp;
  if (!deserializeJson(doc, r.body)) resp = doc["service_response"];
  if (!resp["ok"].is<bool>()) {
    o.message = "No answer from the send script";
    return o;
  }
  if (resp["ok"].as<bool>()) {
    o.sent = true;
    o.message = "Sent to " + toName;
    return o;
  }
  o.reason = resp["reason"].is<const char*>() ? resp["reason"].as<const char*>() : "";
  if (o.reason.size() > 32) o.reason.resize(32);
  o.message = reasonText(o.reason);
  return o;
}

}  // namespace pos::whatsapp
