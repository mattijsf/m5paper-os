#include <doctest.h>

#include <ctime>
#include <string>

#include "apps/inbox/inbox_model.h"
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
  CHECK_FALSE(f.linkDown);
  CHECK(parseFeed(R"({"attributes":{"connected":false,"items":[]}})", f));
  CHECK(f.linkDown);
  CHECK(f.items.empty());
  CHECK_FALSE(parseFeed("", f));
  CHECK_FALSE(parseFeed(R"({"message":"Entity not found."})", f));
  CHECK_FALSE(parseFeed("[1]", f));
  // numeric ids (IMAP uids) and a mail subject
  REQUIRE(parseFeed(R"({"attributes":{"items":[{"id":4821,"from":"a@b.c","subject":"Invoice","text":"x","ts":5,"key":"entry1"}]}})", f));
  CHECK(f.items[0].id == "4821");
  CHECK(f.items[0].subject == "Invoice");
}

TEST_CASE("inbox: feeds are cleaned (emoji, links, HTML mail)") {
  Feed f;
  REQUIRE(parseFeed(R"({"attributes":{"items":[{"from":"A\n","text":"hi ☺ see https://www.example.com/x?y=1","ts":1},
                      {"from":"B","subject":"S\nT","text":"<p>Hello &amp; bye</p><p>Second</p>","ts":2}]}})", f));
  cleanFeed(f, true);
  CHECK(f.items[0].from == "A");
  CHECK(f.items[0].text == "hi :) see example.com");
  CHECK(f.items[1].subject == "S T");
  CHECK(f.items[1].text == "Hello & bye\n\nSecond");
  Feed g;
  REQUIRE(parseFeed(R"({"attributes":{"items":[{"from":"B","text":"<b>not html in chat</b>","ts":2}]}})", g));
  cleanFeed(g, false);
  CHECK(g.items[0].text == "<b>not html in chat</b>");
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
  CHECK(fetchedText(R"({"changed_states":[],"service_response":{"text":"Full body"}})") == "Full body");
  CHECK(fetchedText(R"({"service_response":{"entry1":{"text":"Nested"}}})") == "Nested");
  CHECK(fetchedText("{}") == "");
  CHECK(fetchedText("nope") == "");
}
