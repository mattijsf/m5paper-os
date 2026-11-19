#include <doctest.h>

#include <ctime>
#include <string>

#include "apps/inbox/inbox_model.h"
#include "core/companion.h"
#include "core/timefmt.h"

using namespace pos::inbox;

namespace {
const char* kFeed = R"({"state":"1763200123","attributes":{"updated":"2026-11-15T14:05:00+01:00","connected":true,"items":[
  {"id":"3","from":"Anna","chat":"Family","text":"See you!","ts":1763200100,"key":"g1@g.us","extra":"ignored"},
  {"id":"2","from":"Bram","text":"Hi","ts":1763199000,"key":"b@s.whatsapp.net"},
  {"id":"1","from":"Anna","chat":"Family","text":"Are you coming at six?","ts":1763198000,"key":"g1@g.us"},
  {"id":"0","from":"Nobody","ts":1763197000},
  {"from":"NoId","text":"missing fields are fine"}]}})";
}

TEST_CASE("inbox: feed parsing follows the contract, missing fields default, empty items are dropped") {
  Feed f;
  REQUIRE(parseFeed(kFeed, f));
  REQUIRE(f.items.size() == 4);  // the item with neither text nor subject is dropped
  CHECK(f.items[0].from == "Anna");
  CHECK(f.items[0].chat == "Family");
  CHECK(f.items[0].key == "g1@g.us");
  CHECK(f.items[0].ts == 1763200100);
  CHECK(f.items[1].chat == "");
  CHECK(f.items[3].id == "");
  CHECK(f.items[3].ts == 0);
  CHECK(f.updated == "2026-11-15T14:05:00+01:00");
  CHECK(parseFeed(R"({"attributes":{"connected":false,"items":[]}})", f));
  CHECK(f.items.empty());
  CHECK_FALSE(parseFeed("", f));
  CHECK_FALSE(parseFeed(R"({"message":"Entity not found."})", f));
  CHECK_FALSE(parseFeed("[1]", f));
  // numeric ids (IMAP uids) and a mail subject
  REQUIRE(parseFeed(R"({"attributes":{"items":[{"id":4821,"from":"a@b.c","subject":"Invoice","text":"x","ts":5,"key":"entry1"}]}})", f));
  CHECK(f.items[0].id == "4821");
  CHECK(f.items[0].subject == "Invoice");
}

TEST_CASE("inbox: a notification is cleaned once, when it is stored (emoji, links, HTML mail)") {
  pos::companion::Notif mail;
  mail.app = "mail";
  mail.key = "m1";
  mail.msgs.push_back({"A\n", "", "hi \xE2\x98\xBA see https://www.example.com/x?y=1", 1});
  mail.msgs.push_back({"B", "S\nT", "<p>Hello &amp; bye</p><p>Second</p>", 2});
  pos::companion::cleanNotif(mail);
  CHECK(mail.msgs[0].from == "A");
  CHECK(mail.msgs[0].text == "hi \xE2\x98\xBA see example.com");  // the emoji stays (docs/UI.md "Emoji")
  CHECK(mail.msgs[1].subject == "S T");
  CHECK(mail.msgs[1].text == "Hello & bye\n\nSecond");
  const pos::companion::Notif once = mail;
  pos::companion::cleanNotif(mail);  // cleaning clean text changes nothing
  CHECK(mail.msgs[0].text == once.msgs[0].text);
  CHECK(mail.msgs[1].text == once.msgs[1].text);
  CHECK(mail.msgs[1].subject == once.msgs[1].subject);

  pos::companion::Notif chat;
  chat.app = "whatsapp";
  chat.key = "c1";
  chat.chat = "Family \x01group";
  chat.msgs.push_back({"B", "", "<b>not html in chat</b>", 2});
  pos::companion::cleanNotif(chat);
  CHECK(chat.msgs[0].text == "<b>not html in chat</b>");
  CHECK(chat.chat == "Family group");
  // The feed the inbox shows takes the store's texts as they are.
  pos::companion::Notifs store;
  store.items = {mail, chat};
  CHECK(feedFromNotifs(store, true).items.size() == 2);
  CHECK(feedFromNotifs(store, false).items[0].text == "<b>not html in chat</b>");
}

TEST_CASE("inbox: chats group by key, newest conversation first, new marks follow the seen marker") {
  Feed f;
  REQUIRE(parseFeed(kFeed, f));
  auto c = groupChats(f.items, 1763198500);
  REQUIRE(c.size() == 3);  // Anna-Family (2), Bram, the one without a key
  CHECK(c[0].title == "Anna - Family");
  CHECK(c[0].count == 2);
  CHECK(c[0].lastText == "See you!");
  CHECK(c[0].ts == 1763200100);
  CHECK(c[0].items == std::vector<int>{0, 2});
  CHECK(c[0].isNew);
  CHECK(c[1].title == "Bram");
  CHECK(c[1].isNew);   // 1763199000 > 1763198500
  CHECK(c[2].title == "NoId");
  CHECK_FALSE(c[2].isNew);
  auto none = groupChats(f.items, 1763200100);
  CHECK_FALSE(none[0].isNew);
  CHECK(groupChats({}, 0).empty());
  CHECK(newCount(f.items, 1763198500) == 2);
  CHECK(newCount(f.items, 1763200100) == 0);
  CHECK(newCount(f.items, 0) == 3);  // ts 0 never counts as new
  CHECK(newestTs(f.items) == 1763200100);
}

TEST_CASE("inbox: relative times in the local zone") {
  pos::applyTimezone("CET-1CEST,M3.5.0,M10.5.0/3");
  const int64_t now = 1768480440;  // 2026-01-15 12:34 UTC = 13:34 in Amsterdam
  CHECK(relativeTime(now - 14 * 60, now) == "13:20");
  CHECK(relativeTime(now - 13 * 3600 - 34 * 60 + 60, now) != "");        // earlier today or yesterday: some text
  CHECK(relativeTime(now - 20 * 3600, now) == "Yesterday");               // 17:34 yesterday
  CHECK(relativeTime(now - 3 * 86400, now) == "Mon");                     // Monday 12 Jan 2026
  CHECK(relativeTime(now - 40 * 86400, now) == "6 Dec");
  CHECK(relativeTime(0, now) == "");
  CHECK(relativeTime(now, 0) == "");
  CHECK(relativeTime(now + 5 * 86400, now) == "");                        // a clock far off
  CHECK(firstLine("a\nb") == "a");
  CHECK(firstLine("") == "");
}

TEST_CASE("inbox: items as the live package makes them (chat \"Group\", id is only a key, media placeholder)") {
  Feed f;
  REQUIRE(parseFeed(R"({"state":"1768480000","attributes":{"updated":"2026-01-15T13:34:00+01:00","items":[
    {"id":"1768480000-31612345678-A1B2C3","from":"Anna","chat":"Group","text":"[image]","ts":1768480000,"key":"1203630000@g.us"},
    {"id":"1768479000-31698765432-D4E5F6","from":"Bram","chat":"","text":"Hi","ts":1768479000,"key":"31698765432@s.whatsapp.net"},
    {"id":"1768478000-31612345678-0000AB","from":"Anna","chat":"Group","text":"Earlier","ts":1768478000,"key":"1203630000@g.us"}]}})", f));
  REQUIRE(f.items.size() == 3);
  auto c = groupChats(f.items, 0);
  REQUIRE(c.size() == 2);
  CHECK(c[0].title == "Anna - Group");  // the group name is not in the event
  CHECK(c[0].count == 2);
  CHECK(c[0].lastText == "[image]");
  CHECK(c[1].title == "Bram");
  CHECK(firstLine(c[0].lastText) == "[image]");
}

TEST_CASE("inbox: feeds from the phone's notifications (one app each, newest first, reply only for WhatsApp)") {
  pos::companion::Notifs n;
  n.items.push_back({"wa-1", "whatsapp", "Family", true, 100, {{"Anna", "", "Six?", 90}, {"Bram", "", "Yes", 100}}});
  n.items.push_back({"wa-2", "whatsapp", "", false, 120, {{"Carla", "", "Hi", 120}}});
  n.items.push_back({"mail-1", "mail", "", false, 110, {{"Shop", "Your order", "Ready", 110}}});
  Feed wa = feedFromNotifs(n, false);
  REQUIRE(wa.items.size() == 3);
  CHECK(wa.items[0].from == "Carla");
  CHECK_FALSE(wa.items[0].canReply);
  CHECK(wa.items[1].key == "wa-1");
  CHECK(wa.items[1].canReply);
  CHECK(wa.items[1].chat == "Family");
  auto c = groupChats(wa.items, 0);
  REQUIRE(c.size() == 2);
  CHECK(c[1].count == 2);
  Feed mail = feedFromNotifs(n, true);
  REQUIRE(mail.items.size() == 1);
  CHECK(mail.items[0].subject == "Your order");
  CHECK_FALSE(mail.items[0].canReply);
}

TEST_CASE("inbox: mark all read drops what the list showed, not what came after it, and only that app's") {
  pos::companion::Notifs n;
  n.items.push_back({"wa-1", "whatsapp", "Family", true, 100, {{"Anna", "", "Six?", 90}, {"Bram", "", "Yes", 100}}});
  n.items.push_back({"wa-2", "whatsapp", "", false, 120, {{"Carla", "", "Hi", 120}}});
  n.items.push_back({"wa-3", "whatsapp", "", false, 130, {{"Dirk", "", "Old", 80}, {"Dirk", "", "New", 130}}});  // a message after the list
  n.items.push_back({"mail-1", "mail", "", false, 110, {{"Shop", "Your order", "Ready", 110}}});
  n.items.push_back({"wa-4", "whatsapp", "", false, 95, {}});  // no messages: its own time counts
  // The list showed everything up to 120 (Carla's message).
  const std::vector<std::string> keys = clearUpTo(n, false, 120);
  CHECK(keys == std::vector<std::string>{"wa-1", "wa-2", "wa-4"});
  REQUIRE(n.items.size() == 2);
  CHECK(n.items[0].key == "wa-3");    // Dirk's newer message was never on the screen
  CHECK(n.items[1].key == "mail-1");  // the other app is untouched
  // Mail: its own items only.
  CHECK(clearUpTo(n, true, 200) == std::vector<std::string>{"mail-1"});
  CHECK(n.items.size() == 1);
  CHECK(clearUpTo(n, true, 200).empty());
}
