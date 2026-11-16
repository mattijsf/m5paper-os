#include <doctest.h>

#include <string>

#include "apps/inbox/whatsapp_send.h"

using namespace pos;
using namespace pos::whatsapp;

namespace {
companion::Notifs notifs() {
  companion::Notifs n;
  n.items.push_back({"g-1", "whatsapp", "Family", true, 100, {{"Anna", "", "Six?", 100}}});
  n.items.push_back({"d-1", "whatsapp", "", true, 200, {{"Bram\n", "", "Hi", 200}}});
  n.items.push_back({"d-2", "whatsapp", "", false, 300, {{"Carla", "", "No reply action", 300}}});
  n.items.push_back({"m-1", "mail", "", false, 400, {{"Shop", "Order", "Ready", 400}}});
  n.items.push_back({"g-2", "whatsapp", "Family", true, 500, {{"Dirk", "", "Newer", 500}}});
  return n;
}
}  // namespace

TEST_CASE("whatsapp reply: only WhatsApp notifications with a reply action are targets") {
  const auto n = notifs();
  Target t;
  REQUIRE(replyTarget(n, "g-1", t));
  CHECK(t.name == "Family");
  CHECK(t.key == "g-1");
  REQUIRE(replyTarget(n, "d-1", t));
  CHECK(t.name == "Bram");  // a direct chat: the sender, cleaned
  CHECK_FALSE(replyTarget(n, "d-2", t));
  CHECK_FALSE(replyTarget(n, "m-1", t));
  CHECK_FALSE(replyTarget(n, "nope", t));
}

TEST_CASE("whatsapp reply: a game's score goes to the newest live notification of that chat") {
  const auto n = notifs();
  Target t;
  REQUIRE(findByName(n, "Family", t));
  CHECK(t.key == "g-2");
  REQUIRE(findByName(n, " Bram ", t));
  CHECK(t.key == "d-1");
  CHECK_FALSE(findByName(n, "Carla", t));  // no reply action
  CHECK_FALSE(findByName(n, "Woordle challenge", t));
  companion::Notif long_{"x", "whatsapp", "A name that is much longer than forty characters in total", true, 1, {{"A", "", "t", 1}}};
  CHECK(chatName(long_) == "A name that is much longer than forty ch");
}

TEST_CASE("whatsapp reply: trim and the outcome of the push") {
  CHECK(trimmed("  hi \n") == "hi");
  CHECK(trimmed("\n\t ") == "");
  CHECK(trimmed("a\nb") == "a\nb");
  HaResult ok;
  ok.status = 200;
  CHECK(pushOutcome(ok).sent);
  CHECK(pushOutcome(ok).message == "Sent to your phone");
  HaResult missing;
  missing.status = 400;
  missing.error = HaError::Other;
  CHECK_FALSE(pushOutcome(missing).sent);
  CHECK(pushOutcome(missing).message == "Not sent: no relay in Home Assistant");
  HaResult timeout;
  timeout.error = HaError::Timeout;
  CHECK(pushOutcome(timeout).message == "No answer: it may still reach your phone");
  HaResult wifi;
  wifi.error = HaError::NoWifi;
  CHECK(pushOutcome(wifi).message == "Not sent: No Wi-Fi");
}
