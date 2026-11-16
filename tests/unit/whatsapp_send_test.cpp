#include <doctest.h>

#include <ArduinoJson.h>

#include <string>

#include "apps/inbox/whatsapp_send.h"

using namespace pos;
using namespace pos::whatsapp;

TEST_CASE("whatsapp send: chat JIDs the script accepts, and the ones it refuses") {
  CHECK(validChat("31612345678@s.whatsapp.net"));
  CHECK(validChat("120363012345678901@g.us"));
  CHECK(validChat("31612345678-1600000000@g.us"));  // old-style group id
  CHECK(isGroup("120363012345678901@g.us"));
  CHECK_FALSE(isGroup("31612345678@s.whatsapp.net"));
  for (const char* bad : {"", "31612345678", "31612345678@lid", "a@s.whatsapp.net", "+31612345678@s.whatsapp.net", "1234@s.whatsapp.net",
                          "31612345678@s.whatsapp.net,31600000000@s.whatsapp.net", "status@broadcast", "123456@g.us.evil",
                          "31612345678@s.whatsapp.netx", " 31612345678@s.whatsapp.net", "31612345678-@g.us", "31612345678-16000x0000@g.us",
                          "31612345678-1600000000@s.whatsapp.net", "1234567890123456789012345@s.whatsapp.net"})
    CHECK_MESSAGE(!validChat(bad), bad);
}

TEST_CASE("whatsapp send: trim and the request body (JSON-escaped, trimmed text)") {
  CHECK(trimmed("  hi \n") == "hi");
  CHECK(trimmed("\n\t ") == "");
  CHECK(trimmed("a\nb") == "a\nb");
  const std::string body = sendBody(Target{"Family", "120363012345678901@g.us"}, "  Score \"42\"\nline two\n ");
  JsonDocument doc;
  REQUIRE_FALSE(deserializeJson(doc, body));
  CHECK(std::string(doc["chat"].as<const char*>()) == "120363012345678901@g.us");
  CHECK(std::string(doc["text"].as<const char*>()) == "Score \"42\"\nline two");
  CHECK(doc.as<JsonObjectConst>().size() == 2);  // nothing else goes to the script
}

TEST_CASE("whatsapp send: favourites from the config keep valid chats only, names cleaned and cut") {
  Config c;
  c.inbox.whatsappFavorites = {{"Family", "120363012345678901@g.us"},
                               {"Bad", "31612345678"},
                               {"  Bram\n", "31600000002@s.whatsapp.net"},
                               {"A name that is much longer than forty characters in total", "31600000003@s.whatsapp.net"},
                               {"\n ", "31600000004@s.whatsapp.net"}};
  const auto f = favorites(c);
  REQUIRE(f.size() == 3);
  CHECK(f[0].name == "Family");
  CHECK(f[1].name == "Bram");
  CHECK(f[1].chat == "31600000002@s.whatsapp.net");
  CHECK(f[2].name == "A name that is much longer than forty ch");
}

TEST_CASE("whatsapp send: the script's answer becomes sent, a refusal with its reason, or an error") {
  HaResult ok;
  ok.status = 200;
  ok.body = R"({"changed_states":[],"service_response":{"ok":true,"reason":"","message_id":"3EB0"}})";
  SendOutcome o = sendOutcome(ok, "Family");
  CHECK(o.sent);
  CHECK(o.message == "Sent to Family");

  HaResult refused = ok;
  refused.body = R"({"changed_states":[],"service_response":{"ok":false,"reason":"too_soon","message_id":""}})";
  o = sendOutcome(refused, "Family");
  CHECK_FALSE(o.sent);
  CHECK(o.reason == "too_soon");
  CHECK(o.message == "Not sent: wait 10 seconds");
  for (const char* r : {"empty", "too_long", "bad_chat", "hourly_limit", "send_failed"}) CHECK(reasonText(r).rfind("Not sent: ", 0) == 0);
  CHECK(reasonText("something new") == "Not sent");

  HaResult noResponse = ok;  // a script that ended without its response (or an old Home Assistant)
  noResponse.body = R"({"changed_states":[]})";
  o = sendOutcome(noResponse, "Family");
  CHECK_FALSE(o.sent);
  CHECK(o.message == "No answer from the send script");
  noResponse.body = "not json";
  CHECK_FALSE(sendOutcome(noResponse, "Family").sent);

  HaResult missing;
  missing.status = 400;
  missing.error = HaError::Other;
  CHECK(sendOutcome(missing, "Family").message == "Not sent: no send script in Home Assistant");
  HaResult timeout;
  timeout.error = HaError::Timeout;
  CHECK(sendOutcome(timeout, "Family").message == "No answer: check WhatsApp first");
  HaResult wifi;
  wifi.error = HaError::NoWifi;
  CHECK(sendOutcome(wifi, "Family").message == "Not sent: No Wi-Fi");
}
