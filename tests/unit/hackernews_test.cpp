// Hacker News (docs/apps/hackernews.md): the two APIs' answers into stories and a comment tree in reading order, comment
// HTML into marked text, the thread as paragraphs, the saved copy, and the reads through a transport that answers from
// the fixtures (tests/fixtures/make-hackernews-fixtures.py).
#include <doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "apps/hackernews/hackernews_model.h"
#include "core/http_fixture.h"

using namespace pos;
using namespace pos::hackernews;

namespace {

constexpr int64_t kNow = 1768480440;  // 2026-01-15 12:34:00 UTC: the fixtures' "now"

// Answers a request from tests/fixtures/http, as the simulator does; counts what was asked.
struct FixtureNet {
  std::vector<std::string> asked;
  Transport transport() {
    return [this](const hal::HttpRequest& req) {
      asked.push_back(req.url);
      hal::HttpResponse r;
      const std::string name = "tests/fixtures/http/" + fixtureFileName(req.method, req.url);
      std::ifstream f(name);
      if (!f) {
        r.error = "no fixture";
        return r;
      }
      std::stringstream ss;
      ss << f.rdbuf();
      r.body = ss.str();
      r.status = 200;
      std::ifstream st(name.substr(0, name.size() - 5) + ".status");
      if (st) st >> r.status;
      return r;
    };
  }
};

std::string hit(uint32_t id, uint32_t parent, const char* by, const char* text) {
  return "{\"author\":\"" + std::string(by) + "\",\"comment_text\":" + (text ? "\"" + std::string(text) + "\"" : "null") +
         ",\"created_at_i\":" + std::to_string(kNow - 600) + ",\"objectID\":\"" + std::to_string(id) + "\",\"parent_id\":" +
         std::to_string(parent) + "}";
}
std::string hits(const std::vector<std::string>& list, int nbHits = -1) {
  std::string s = "{\"hits\":[";
  for (size_t i = 0; i < list.size(); ++i) s += (i ? "," : "") + list[i];
  return s + "],\"nbHits\":" + std::to_string(nbHits < 0 ? static_cast<int>(list.size()) : nbHits) + "}";
}

}  // namespace

TEST_CASE("hackernews: sections and addresses") {
  CHECK(std::string(sectionName(Section::Top)) == "Top");
  CHECK(std::string(sectionKey(Section::Show)) == "show");
  Section s = Section::Top;
  CHECK(sectionFromKey("ask", s));
  CHECK(s == Section::Ask);
  CHECK_FALSE(sectionFromKey("best", s));
  CHECK(s == Section::Ask);
  CHECK(idsUrl(Section::Top) == "https://hacker-news.firebaseio.com/v0/topstories.json");
  CHECK(idsUrl(Section::New) == "https://hacker-news.firebaseio.com/v0/newstories.json");
  CHECK(idsUrl(Section::Ask) == "https://hacker-news.firebaseio.com/v0/askstories.json");
  CHECK(idsUrl(Section::Show) == "https://hacker-news.firebaseio.com/v0/showstories.json");
  CHECK(itemUrl(8863) == "https://hacker-news.firebaseio.com/v0/item/8863.json");
  CHECK(siteUrl(8863) == "https://news.ycombinator.com/item?id=8863");
  CHECK(storiesUrl({11, 22}) ==
        "https://hn.algolia.com/api/v1/search?tags=(story,poll),(story_11,story_22)&hitsPerPage=2"
        "&attributesToRetrieve=title,url,author,points,num_comments,created_at_i&attributesToHighlight=none");
  CHECK(commentsUrl(8863) ==
        "https://hn.algolia.com/api/v1/search?tags=comment,story_8863&hitsPerPage=1000"
        "&attributesToRetrieve=author,comment_text,parent_id,created_at_i&attributesToHighlight=none");
}

TEST_CASE("hackernews: ids") {
  std::vector<uint32_t> ids;
  CHECK(parseIds("[3,1,2]", ids));
  CHECK(ids == std::vector<uint32_t>{3, 1, 2});
  CHECK(parseIds("[5,6,7,8]", ids, 2));
  CHECK(ids == std::vector<uint32_t>{5, 6});
  CHECK(parseIds("[]", ids));
  CHECK(ids.empty());
  for (const char* bad : {"", "null", "{\"error\":\"Permission denied\"}", "[1,2", "<html>"}) CHECK_FALSE(parseIds(bad, ids));
}

TEST_CASE("hackernews: stories come in the order of the ids, unknown ids are left out") {
  const std::string json =
      "{\"hits\":[{\"author\":\"b\",\"created_at_i\":200,\"num_comments\":2,\"objectID\":\"22\",\"points\":20,\"title\":\"Second\"},"
      "{\"author\":\"a\",\"created_at_i\":100,\"num_comments\":1,\"objectID\":\"11\",\"points\":10,\"title\":\"First &amp; \\u2018best\\u2019\","
      "\"url\":\"https://www.example.com/x?y=1\"},"
      "{\"objectID\":\"44\",\"title\":null},{\"objectID\":\"55\",\"title\":\"Not asked for\"}],\"nbHits\":4}";
  std::vector<Story> stories;
  REQUIRE(parseStories(json, {11, 33, 22, 44, 11}, stories));
  REQUIRE(stories.size() == 2);
  CHECK(stories[0].id == 11);
  CHECK(stories[0].title == "First &amp; \xE2\x80\x98" "best\xE2\x80\x99");  // titles are text, not HTML
  CHECK(stories[0].url == "https://www.example.com/x?y=1");
  CHECK(stories[0].by == "a");
  CHECK(stories[0].points == 10);
  CHECK(stories[0].comments == 1);
  CHECK(stories[0].time == 100);
  CHECK(stories[1].id == 22);
  CHECK(stories[1].url.empty());
  REQUIRE(parseStories(json, {22, 11}, stories, 1));
  REQUIRE(stories.size() == 1);
  CHECK(stories[0].id == 22);
  CHECK_FALSE(parseStories("[1,2]", {1}, stories));
  CHECK_FALSE(parseStories("", {1}, stories));
}

TEST_CASE("hackernews: an item gives the story's text and the order of its comments") {
  Story s;
  s.title = "From the list";
  s.url = "https://example.com/kept";
  std::vector<uint32_t> kids{9};
  REQUIRE(parseItem("{\"by\":\"pg\",\"descendants\":15,\"id\":8863,\"kids\":[30,10,20],\"score\":104,\"time\":1175714200,"
                    "\"text\":\"One.<p>Two &amp; three.\",\"title\":\"My story\",\"type\":\"story\"}",
                    s, kids));
  CHECK(s.id == 8863);
  CHECK(s.title == "My story");
  CHECK(s.url == "https://example.com/kept");  // the item names none: what the list knew stays
  CHECK(s.by == "pg");
  CHECK(s.points == 104);
  CHECK(s.comments == 15);
  CHECK(s.time == 1175714200);
  CHECK(s.text == "One.\nTwo & three.");
  CHECK(kids == std::vector<uint32_t>{30, 10, 20});
  Story keep;
  keep.title = "kept";
  CHECK_FALSE(parseItem("null", keep, kids));
  CHECK_FALSE(parseItem("", keep, kids));
  CHECK(keep.title == "kept");
}

TEST_CASE("hackernews: comment HTML to marked text") {
  CHECK(markedText("Plain text.") == "Plain text.");
  CHECK(markedText("One.<p>Two.<p>Three.") == "One.\nTwo.\nThree.");
  CHECK(markedText("It&#x27;s <i>very</i> &quot;quoted&quot; &amp; 1 &lt; 2 &gt; 0") == "It's very \"quoted\" & 1 < 2 > 0");
  CHECK(markedText("See <a href=\"https:&#x2F;&#x2F;example.com&#x2F;a\" rel=\"nofollow\">https:&#x2F;&#x2F;example.com&#x2F;a</a> now") ==
        "See https://example.com/a now");
  // A code block: its lines stay lines (0x06: continues the block), what a line is moved in by stays as no-break spaces.
  CHECK(markedText("Like so:<p><pre><code>if x:\n    y()\n\nz()\n</code></pre>\nDone.") ==
        "Like so:\nif x:\n\x06\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0y()\n\x06z()\nDone.");
  CHECK(markedText("") == "");
  CHECK(markedText("<p><p>") == "");
  CHECK(markedText("a\x01\x05" "b") == "ab");  // no control bytes: they are marks in marked text
}

TEST_CASE("hackernews: comments become a tree in reading order") {
  // Story 1. Answers 10 and 20 (the site puts 20 first) and 40, which the site's list does not name; replies 11 and 12
  // under 10 (12 first in the answer, 11 older); 13 under 11; 99 answers a comment that is not there; 30 was deleted and
  // nobody answered, 50 was deleted and has a reply.
  const std::string json = hits({hit(12, 10, "d", "reply two"), hit(10, 1, "a", "first"), hit(13, 11, "e", "deep"), hit(20, 1, "b", "second"),
                                 hit(11, 10, "c", "reply one"), hit(99, 77, "x", "lost"), hit(30, 1, "y", nullptr), hit(40, 1, "f", "late"),
                                 hit(50, 1, "z", nullptr), hit(51, 50, "g", "answer to nothing")},
                                10);
  Thread t;
  REQUIRE(parseComments(json, 1, {20, 10, 30}, t));
  CHECK(t.total == 10);
  std::vector<uint32_t> order;
  std::vector<int> depth, replies;
  for (const Comment& c : t.comments) {
    order.push_back(c.id);
    depth.push_back(c.depth);
    replies.push_back(c.replies);
  }
  CHECK(order == std::vector<uint32_t>{20, 10, 11, 13, 12, 40, 50, 51});
  CHECK(depth == std::vector<int>{0, 0, 1, 2, 1, 0, 0, 1});
  CHECK(replies == std::vector<int>{0, 3, 1, 0, 0, 0, 1, 0});
  CHECK(t.comments[1].by == "a");
  CHECK(t.comments[1].text == "first");
  CHECK(t.comments[1].time == kNow - 600);
  CHECK(t.comments[6].text == "[deleted]");
  // Without the site's order: oldest first.
  REQUIRE(parseComments(json, 1, {}, t));
  CHECK(t.comments.front().id == 10);
  // A thread a thousand replies deep (no recursion).
  std::vector<std::string> chain;
  for (uint32_t i = 0; i < 1000; ++i) chain.push_back(hit(1000 + i, i == 0 ? 1 : 999 + i, "n", "x"));
  REQUIRE(parseComments(hits(chain), 1, {}, t));
  REQUIRE(t.comments.size() == 1000);
  CHECK(t.comments.back().depth == 999);
  CHECK(t.comments.front().replies == 999);
  REQUIRE(parseComments(hits({}), 1, {}, t));
  CHECK(t.comments.empty());
  CHECK_FALSE(parseComments("[]", 1, {}, t));
  CHECK_FALSE(parseComments("", 1, {}, t));
}

TEST_CASE("hackernews: ages and the lines under a title") {
  CHECK(ageText(kNow - 30, kNow) == "now");
  CHECK(ageText(kNow + 30, kNow) == "now");
  CHECK(ageText(kNow - 60, kNow) == "1 min");
  CHECK(ageText(kNow - 3599, kNow) == "59 min");
  CHECK(ageText(kNow - 3600, kNow) == "1 h");
  CHECK(ageText(kNow - 47 * 3600, kNow) == "47 h");
  CHECK(ageText(kNow - 48 * 3600, kNow) == "2 d");
  CHECK(ageText(0, kNow) == "");
  CHECK(ageText(kNow, 0) == "");
  Story s;
  s.points = 312;
  s.comments = 141;
  s.time = kNow - 5 * 3600;
  s.url = "https://www.example.com/a/b";
  s.by = "pg";
  CHECK(metaLine(s, kNow) == "312 points \xC2\xB7 141 comments \xC2\xB7 5 h \xC2\xB7 example.com");
  CHECK(storyLine(s, kNow) == "312 points \xC2\xB7 by pg \xC2\xB7 5 h \xC2\xB7 example.com");
  s.points = 1;
  s.comments = 1;
  s.url.clear();
  s.time = 0;
  CHECK(metaLine(s, kNow) == "1 point \xC2\xB7 1 comment");
}

TEST_CASE("hackernews: a thread as paragraphs, replies moved right, bylines kept with their text") {
  Story story;
  story.text = "The story's own text.\nIts second paragraph.";
  Thread t;
  REQUIRE(parseComments(hits({hit(10, 1, "a", "first<p>more"), hit(11, 10, "c", "reply"), hit(20, 1, "b", "second")}), 1, {}, t));
  for (int depth = 1; depth < 12; ++depth) {  // a reply to a reply to a reply...
    Comment c;
    c.id = 100 + depth;
    c.by = "deep";
    c.text = "x";
    c.depth = depth;
    t.comments.push_back(c);
  }
  const ThreadText tt = threadText(story, t, kNow);
  REQUIRE(tt.paras.size() == 2 + 3 + 2 + 2 + 11 * 2);
  CHECK(ui::plainText(tt.paras[0]) == "The story's own text.");
  CHECK(tt.paras[0].margin == 0);
  CHECK(tt.paras[2].heading == 4);
  CHECK(ui::plainText(tt.paras[2]) == "a \xC2\xB7 10 min \xC2\xB7 1 reply");
  CHECK(ui::plainText(tt.paras[3]) == "first");
  CHECK(ui::plainText(tt.paras[4]) == "more");
  CHECK(tt.paras[4].heading == 0);
  CHECK(ui::plainText(tt.paras[5]) == "c \xC2\xB7 10 min");
  CHECK(tt.paras[5].margin == kDepthStep);
  CHECK(tt.paras[6].margin == kDepthStep);
  CHECK(tt.paras[7].margin == 0);
  CHECK(tt.paras.back().margin == kDepthMax * kDepthStep);  // deeper than that stays there
  CHECK(tt.topLevel == std::vector<int>{2, 7});
  CHECK(nextTopLevel(tt, -1) == 2);
  CHECK(nextTopLevel(tt, 1) == 2);
  CHECK(nextTopLevel(tt, 2) == 7);
  CHECK(nextTopLevel(tt, 6) == 7);
  CHECK(nextTopLevel(tt, 7) == -1);
  // A place survives a new read: the comment and how far into it.
  CHECK(tt.ids.size() == t.comments.size());
  Anchor a = anchorOf(tt, ui::TextPos{4, 2});  // "more", the second paragraph of comment 10
  CHECK(a.comment == 10);
  CHECK(a.para == 2);
  CHECK(a.byte == 2);
  CHECK(posOf(tt, a) == ui::TextPos{4, 2});
  a = anchorOf(tt, ui::TextPos{1, 5});  // in the story's own text
  CHECK(a.comment == 0);
  CHECK(posOf(tt, a) == ui::TextPos{1, 5});
  {
    Thread newer;  // read again: a comment came before it, the story's text is gone
    REQUIRE(parseComments(hits({hit(5, 1, "new", "newer"), hit(10, 1, "a", "first<p>more"), hit(20, 1, "b", "second")}), 1, {5, 10, 20}, newer));
    const ThreadText again = threadText(Story{}, newer, kNow);
    CHECK(posOf(again, anchorOf(tt, ui::TextPos{4, 2})) == ui::TextPos{4, 2});  // byline 5, text, byline 10, "first", "more"
    CHECK(posOf(again, anchorOf(tt, ui::TextPos{5, 0})) == ui::TextPos{});       // comment 11 is gone: the start
    CHECK(posOf(again, Anchor{20, 7, 0}) == ui::TextPos{5, 0});                  // further than the comment is long: its byline
    CHECK(posOf(again, Anchor{0, 1, 5}) == ui::TextPos{});                       // no text of the story any more
  }
  // More comments than one answer holds: said at the end.
  t.total = 1500;
  const ThreadText more = threadText(story, t, kNow);
  CHECK(ui::plainText(more.paras.back()) == "The first 1000 of 1500 comments are shown.");
  // No text and no comments: nothing.
  CHECK(threadText(Story{}, Thread{}, kNow).paras.empty());
}

TEST_CASE("hackernews: the saved copy of a list") {
  std::vector<Story> stories(2);
  stories[0].id = 11;
  stories[0].title = "A \"quoted\" title \xE2\x80\x94 with a dash";
  stories[0].url = "https://example.com/a";
  stories[0].by = "someone";
  stories[0].points = 12;
  stories[0].comments = 3;
  stories[0].time = kNow - 99;
  stories[0].text = "not kept";
  stories[1].id = 22;
  stories[1].title = "Second";
  const std::string packed = packList(kNow, stories);
  CHECK(packed.find("not kept") == std::string::npos);
  int64_t fetched = 0;
  std::vector<Story> back;
  REQUIRE(unpackList(packed, fetched, back));
  CHECK(fetched == kNow);
  REQUIRE(back.size() == 2);
  CHECK(back[0].id == 11);
  CHECK(back[0].title == stories[0].title);
  CHECK(back[0].url == stories[0].url);
  CHECK(back[0].by == "someone");
  CHECK(back[0].points == 12);
  CHECK(back[0].comments == 3);
  CHECK(back[0].time == kNow - 99);
  CHECK(back[0].text.empty());
  CHECK(back[1].title == "Second");
  for (const char* bad : {"", "null", "[]", "{\"t\":1}", "{\"s\":3}", "{\"t\":1,\"s\":[[1,\"x\""}) CHECK_FALSE(unpackList(bad, fetched, back));
}

TEST_CASE("hackernews: a list is two requests, the job advert among the ids is not shown") {
  FixtureNet net;
  const ListResult r = fetchList(net.transport(), Section::Top);
  REQUIRE(r.ok);
  CHECK(net.asked.size() == 2);
  REQUIRE(r.stories.size() == kListMax);
  CHECK(r.stories[0].id == 38250001);
  CHECK(r.stories[0].title == "A file system that fits on a postcard");
  CHECK(r.stories[0].points == 412);
  CHECK(metaLine(r.stories[0], kNow) == "412 points \xC2\xB7 14 comments \xC2\xB7 3 h \xC2\xB7 example.com");
  CHECK(r.stories[1].url.empty());  // an Ask HN
  CHECK(r.stories[2].id == 38250003);  // the advert stood between the second and the third
  CHECK(r.stories[29].id == 38250030);
  FixtureNet net2;
  const ListResult n = fetchList(net2.transport(), Section::New);
  REQUIRE(n.ok);
  CHECK(n.stories.size() == 8);
}

TEST_CASE("hackernews: a list that can not be read says why") {
  const auto fail = [](int status, const char* error, const char* body) {
    return [=](const hal::HttpRequest&) {
      hal::HttpResponse r;
      r.status = status;
      r.error = error;
      r.body = body;
      return r;
    };
  };
  CHECK(fetchList(fail(0, "No Wi-Fi: No known network in range", ""), Section::Top).error == "No Wi-Fi: No known network in range");
  CHECK(fetchList(fail(0, "", ""), Section::Top).error == "No connection");
  CHECK(fetchList(fail(503, "", "busy"), Section::Top).error == "Hacker News answered 503");
  CHECK(fetchList(fail(200, "", "<html>"), Section::Top).error == "Unexpected answer");
  CHECK(fetchList(fail(200, "response too large", "[1,2"), Section::Top).error == "response too large");
  const ListResult none = fetchList(fail(200, "", "[]"), Section::Top);
  CHECK(none.ok);  // a list with nothing in it: one request
  CHECK(none.stories.empty());
}

TEST_CASE("hackernews: a story's comments are two requests, in the site's order") {
  FixtureNet net;
  Story known;
  known.id = 38250001;
  known.title = "A file system that fits on a postcard";
  const ThreadResult r = fetchThread(net.transport(), known);
  REQUIRE(r.ok);
  CHECK(net.asked.size() == 2);
  CHECK(r.story.points == 412);
  CHECK(r.story.by == "marit");
  REQUIRE(r.thread.comments.size() == 13);  // 15 hits: one deleted without an answer, one whose parent is not there
  std::vector<uint32_t> top;
  for (const Comment& c : r.thread.comments)
    if (c.depth == 0) top.push_back(c.id);
  CHECK(top == std::vector<uint32_t>{38250108, 38250101, 38250113, 38250102, 38250111});
  CHECK(r.thread.comments[0].by == "duty_cycle");
  CHECK(r.thread.comments[0].replies == 2);
  CHECK(r.thread.comments[1].depth == 1);
  int deepest = 0;
  for (const Comment& c : r.thread.comments) deepest = std::max(deepest, c.depth);
  CHECK(deepest == 4);
  const ThreadText tt = threadText(r.story, r.thread, kNow);
  CHECK(tt.topLevel.size() == 5);
  CHECK(ui::plainText(tt.paras[0]) == "duty_cycle \xC2\xB7 2 h \xC2\xB7 2 replies");

  // An Ask HN: its own text comes with the item.
  FixtureNet ask;
  known = Story{};
  known.id = 38250002;
  const ThreadResult a = fetchThread(ask.transport(), known);
  REQUIRE(a.ok);
  CHECK(a.story.title == "Ask HN: What did you stop automating?");
  CHECK(a.story.text.find("For years I scripted everything") == 0);
  CHECK(a.thread.comments.size() == 3);
  CHECK(a.thread.comments[0].by == "rackunit");

  // A story without comments: the search is not asked.
  FixtureNet quiet;
  known = Story{};
  known.id = 38250003;
  const ThreadResult q = fetchThread(quiet.transport(), known);
  REQUIRE(q.ok);
  CHECK(quiet.asked.size() == 1);
  CHECK(q.thread.comments.empty());

  // The search does not answer: the reason.
  FixtureNet down;
  known = Story{};
  known.id = 38250004;
  const ThreadResult d = fetchThread(down.transport(), known);
  CHECK_FALSE(d.ok);
  CHECK(d.error == "Hacker News answered 503");
  CHECK(d.story.title == "The long tail of time zone bugs");  // what the first answer gave is kept
}
