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
  // The two blanks the site wants before every line of code are not an indentation.
  CHECK(markedText("<pre><code>  a = 1\n    b = 2\n  c = 3\n</code></pre>") == "a = 1\n\x06\xC2\xA0\xC2\xA0" "b = 2\n\x06" "c = 3");
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

  // Asked again right after a list (the connection is on the search service) with the comment count the list gave: the
  // comments first, then the item. The same result.
  {
    FixtureNet after;
    const ListResult list = fetchList(after.transport(), Section::Top);
    REQUIRE(list.ok);
    after.asked.clear();
    const ThreadResult again = fetchThread(after.transport(), list.stories[0]);
    REQUIRE(again.ok);
    REQUIRE(after.asked.size() == 2);
    CHECK(after.asked[0] == commentsUrl(38250001));
    CHECK(after.asked[1] == itemUrl(38250001));
    CHECK(again.thread.comments.size() == 13);
    CHECK(again.thread.comments[0].by == "duty_cycle");  // still the site's order
    // The next story: the connection is on the order service now, so the item first.
    after.asked.clear();
    REQUIRE(fetchThread(after.transport(), list.stories[1]).ok);
    REQUIRE(after.asked.size() == 2);
    CHECK(after.asked[0] == itemUrl(38250002));
  }

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

TEST_CASE("hackernews: Markdown of an article to marked text") {
  CHECK(articleMarked("One line.") == "One line.");
  CHECK(articleMarked("A paragraph\nthat goes on.\n\nAnother.\n") == "A paragraph that goes on.\nAnother.");
  // Headings: # and ## are level 2, deeper ones 3; a line underlined with = or - is one too.
  CHECK(articleMarked("# Title\n\n## Part ##\n\n### Detail\n\nText") == "\x05" "2Title\n\x05" "2Part\n\x05" "3Detail\nText");
  CHECK(articleMarked("Title\n=====\n\nSub\n---\n\nText") == "\x05" "2Title\n\x05" "2Sub\nText");
  CHECK(articleMarked("#hashtag is text") == "#hashtag is text");
  // Links are their words, pictures go (the second reader service keeps both).
  CHECK(articleMarked("See [the **docs**](https://x.example/a_(b)) and ![a chart](c.png) [1] `[a](b)`.") ==
        "See the \x01" "docs\x02 and [1] [a](b).");
  CHECK(articleMarked("[![logo](l.png)](https://x.example/)\n\nText") == "Text");
  CHECK(articleMarked("[ Subscribe now ](https://x.example/s) [unclosed](x") == "Subscribe now [unclosed](x");
  // What a page's source left (a lone tag, a template, a heading without words) goes; a short paragraph only once.
  CHECK(articleMarked("<!DOCTYPE html>\n\nSkip\n\n{{ message }}\n\n###\n\n### Note\n\nShare this\n\nA comment.\n\n### Note\n\n"
                      "Share this\n\n<https://x.example/>\n\n<b>bold</b> text", true) ==
        "Skip\n\x05" "3Note\nShare this\nA comment.\n<https://x.example/>\n<b>bold</b> text");
  const std::string longer = "A paragraph long enough to be text and not furniture, said twice on purpose.";
  CHECK(articleMarked(longer + "\n\n" + longer, true) == longer + "\n" + longer);
  // GitHub's furniture, which the second service keeps (its session banner is hidden in a browser).
  CHECK(articleMarked("Skip to content\n\nYou signed in with another tab or window. Reload to refresh your session. Dismiss alert\n\n"
                      "**nmouha** commented Nov 10, 2026\n\nCopy link Copy Markdown\n\nContributor\n\nThe fix.\n\n"
                      "Sorry, something went wrong.\n\n### Uh oh!\n\nThere was an error while loading. Please reload this page.", true) ==
        "\x01nmouha\x02 commented Nov 10, 2026\nThe fix.");
  CHECK(articleMarked("Skip to content\n\nText.") == "Skip to content\nText.");  // a file of the user's
  CHECK(articleMarked("Refrain.\n\nVerse.\n\nRefrain.\n\n{{ kept }}") == "Refrain.\nVerse.\nRefrain.\n{{ kept }}");  // a file of the user's
  // Emphasis: bold is marked, the marks of emphasis, inline code and escapes go; signs and snake_case stay.
  CHECK(articleMarked("A **bold** word, an _emphasised_ one, `code_here` and 2 * 3 = 6.") ==
        "A \x01" "bold\x02 word, an \x03" "emphasised\x04 one, code_here and 2 * 3 = 6.");
  CHECK(articleMarked("snake_case stays, \\*not emphasis\\*, a\\_b") == "snake_case stays, *not emphasis*, a_b");
  CHECK(articleMarked("**never closed") == "\x01never closed\x02");
  // Lists: a bullet or the number, an item moved in by its level; the lines of an item are joined.
  CHECK(articleMarked("*   first\n*   second\n    goes on\n    *   inner\n1.  one\n12. twelve\n- dash") ==
        "\xE2\x80\xA2 first\n\xE2\x80\xA2 second goes on\n\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0\xE2\x80\xA2 inner\n1. one\n12. twelve\n\xE2\x80\xA2 dash");
  // A line across is dropped; a quote keeps its sign.
  CHECK(articleMarked("above\n\n* * *\n\nbelow\n\n---\n\n> quoted\n> on two lines\n\nafter") == "above\nbelow\n> quoted on two lines\nafter");
  // Code: lines stay lines (0x06), what a line is moved in by stays, nothing in it is a mark.
  CHECK(articleMarked("Before:\n\n```c\nint *p = a_b;\n    return **p;\n\n```\nAfter") ==
        "Before:\nint *p = a_b;\n\x06\xC2\xA0\xC2\xA0\xC2\xA0\xC2\xA0return **p;\nAfter");
  // A table: a row a line, its cells with a dot between, the row of dashes dropped.
  CHECK(articleMarked("| Name | Blocks |\n| --- | --- |\n| Rename | 2 |\n\nAfter") ==
        "Name \xC2\xB7 Blocks\n\x06Rename \xC2\xB7 2\nAfter");
  CHECK(articleMarked("") == "");
  CHECK(articleMarked("\n\n---\n\n") == "");
}

TEST_CASE("hackernews: the reader's answer and request") {
  const hal::HttpRequest req = articleRequest("https://www.example.com/a?b=\"c\"");
  CHECK(req.method == "POST");
  CHECK(req.url == "https://r.jina.ai/");
  CHECK(req.body == "{\"url\":\"https://www.example.com/a?b=\\\"c\\\"\"}");
  bool noImages = false, json = false;
  for (const auto& h : req.headers) {
    noImages = noImages || (h.first == "X-Retain-Images" && h.second == "none");
    json = json || (h.first == "Content-Type" && h.second == "application/json");
  }
  CHECK(noImages);
  CHECK(json);

  Article a;
  REQUIRE(parseArticle("Title: The page\n\nURL Source: https://example.com/x\n\nMarkdown Content:\nFirst paragraph.\n\n## Part\n\nSecond.\n", a));
  CHECK(a.title == "The page");
  CHECK(a.text == "\x05" "2The page\nFirst paragraph.\n\x05" "2Part\nSecond.");  // the title: the text starts with none
  REQUIRE(parseArticle("Title: The page\n\nMarkdown Content:\n# The page, again\n\nText.\n", a));
  CHECK(a.text == "\x05" "2The page, again\nText.");
  REQUIRE(parseArticle("Title: Blocked\n\nWarning: Target URL returned error 403: Forbidden\n\nMarkdown Content:\nJust a moment...\n", a));
  CHECK(a.text == "\x05" "4Target URL returned error 403: Forbidden\n\x05" "2Blocked\nJust a moment...");
  REQUIRE(parseArticle("Title: Fix a bug \xC2\xB7 Pull Request #1\n\nMarkdown Content:\n## Conversation\n\nText.\n", a));
  CHECK(a.text == "\x05" "2Fix a bug \xC2\xB7 Pull Request #1\n\x05" "2Conversation\nText.");  // a heading that is not the title
  REQUIRE(parseArticle("Title: The heading | Site\n\nMarkdown Content:\n# The Heading\n\nText.\n", a));
  CHECK(a.text == "\x05" "2The Heading\nText.");
  REQUIRE(parseArticle("No marker at all, only text.", a));
  CHECK(a.text == "No marker at all, only text.");
  CHECK_FALSE(parseArticle("Title: Empty\n\nMarkdown Content:\n\n", a));
  CHECK_FALSE(parseArticle("", a));
}

TEST_CASE("hackernews: an article is one request when the first service reads it; its own words when it fails") {
  FixtureNet net;
  hal::HttpRequest sent;
  const Transport fixtures = net.transport();
  const Transport send = [&](const hal::HttpRequest& req) {
    sent = req;
    hal::HttpResponse r;
    // (the fixtures of a request with a body are named after the body)
    const std::string name = "tests/fixtures/http/" + fixtureFileName(req.method, req.url, req.body);
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
  const ArticleResult r = fetchArticle(send, "https://www.example.com/postcard-fs");
  REQUIRE(r.ok);
  CHECK(sent.method == "POST");
  CHECK(r.article.title == "A file system that fits on a postcard");
  const std::vector<ui::Paragraph> paras = ui::parseMarked(r.article.text);
  REQUIRE(paras.size() > 20);
  CHECK(paras[0].heading == 2);
  CHECK(ui::plainText(paras[0]) == "A file system that fits on a postcard");
  CHECK(ui::plainText(paras[1]) == "By Marit, January 2026");
  bool bold = false, code = false;
  for (const ui::Paragraph& p : paras) {
    for (const ui::Run& run : p.runs) bold = bold || (run.bold && run.text == "postcard");
    code = code || (p.softBreak && ui::plainText(p).find("uint16_t next;") != std::string::npos);
  }
  CHECK(bold);
  CHECK(code);
  CHECK(ui::plainText(paras.back()).find("snake_case and all") != std::string::npos);

  const ArticleResult bad = fetchArticle(send, "https://blog.example.net/2023/time-zones");
  CHECK_FALSE(bad.ok);
  CHECK(bad.error == "Domain 'blog.example.net' could not be resolved");
  const auto fail = [](int status, const char* error, std::string body) {
    return [=](const hal::HttpRequest&) {
      hal::HttpResponse r;
      r.status = status;
      r.error = error;
      r.body = body;
      return r;
    };
  };
  CHECK(fetchArticle(fail(0, "No Wi-Fi: No known network in range", ""), "https://x.example/").error == "No Wi-Fi: No known network in range");
  CHECK(fetchArticle(fail(503, "", "<html>"), "https://x.example/").error == "The reader answered 503");
  CHECK(fetchArticle(fail(200, "", "Title: x\n\nMarkdown Content:\n"), "https://x.example/").error == "No text on the page");
  CHECK(fetchArticle(fail(200, "", std::string(kMaxArticleBytes + 1, 'a')), "https://x.example/").error == "The page is too long");
}

TEST_CASE("hackernews: the second reader service, when the first can not read the page") {
  const hal::HttpRequest req = fallbackRequest("https://w.example/a");
  CHECK(req.method == "POST");
  CHECK(req.url == "https://markdown.new/");
  CHECK(req.body == "{\"url\":\"https://w.example/a\"}");
  bool timeout = false;
  for (const auto& h : articleRequest("https://w.example/a").headers) timeout = timeout || (h.first == "X-Timeout" && h.second == "6");
  CHECK(timeout);  // a CAPTCHA page holds the first service 15 s without it

  // Its answer is JSON with the page as Markdown after a YAML front matter: in the first service's format.
  const std::string page = "{\"success\":true,\"url\":\"https://w.example/a\",\"title\":\"The \\\"real\\\"\\npage\",\"content\":"
                           "\"---\\ntitle: \\\"The \\\\\\\"real\\\\\\\" page\\\"\\nmeta:\\n  author: x\\n---\\n\\nIt has text.\\n\\n## More\\n\\nAnd more.\"}";
  CHECK(fallbackAnswer(page, "https://w.example/a") ==
        "Title: The \"real\" page\n\nURL Source: https://w.example/a\n\nMarkdown Content:\n---\ntitle: \"The \\\"real\\\" page\"\nmeta:\n  author: x\n---\n\n"
        "It has text.\n\n## More\n\nAnd more.");
  CHECK(fallbackAnswer("{\"success\":false,\"error\":\"Error from The API: All conversion methods failed.\"}", "https://w.example/a") == "");
  CHECK(fallbackAnswer("{\"success\":true,\"content\":\"\"}", "https://w.example/a") == "");
  CHECK(fallbackAnswer("<html>", "https://w.example/a") == "");

  Article a;
  REQUIRE(parseArticle(fallbackAnswer(page, "https://w.example/a"), a));
  CHECK(a.title == "The \"real\" page");
  CHECK(a.text == "\x05" "2The \"real\" page\nIt has text.\n\x05" "2More\nAnd more.");
  REQUIRE(parseArticle("Markdown Content:\n---\ntitle: 'It''s here'\n---\nText.", a));  // the title from the front matter
  CHECK(a.title == "It's here");
  CHECK(a.text == "\x05" "2It's here\nText.");
  REQUIRE(parseArticle("Markdown Content:\n---\n\nAfter a rule.", a));  // a rule, not a front matter
  CHECK(a.text == "After a rule.");
  REQUIRE(parseArticle("Markdown Content:\n---\nNot: yaml at all\n\nText.", a));  // never closed: text
  CHECK(ui::plainText(ui::parseMarked(a.text).front()) == "Not: yaml at all");
  CHECK_FALSE(parseArticle("Markdown Content:\n---\ntitle: reuters.com\n---", a));  // only a front matter: the site refused it

  std::vector<std::string> asked;
  const auto answer = [](int status, std::string body, std::string error = "", bool notSent = false) {
    hal::HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    r.error = std::move(error);
    r.notSent = notSent;
    return r;
  };
  const auto services = [&](hal::HttpResponse first, hal::HttpResponse second) {
    asked.clear();
    return Transport([&asked, first, second](const hal::HttpRequest& r) {
      asked.push_back(r.url);
      return r.url == kReaderUrl ? first : second;
    });
  };
  const hal::HttpResponse good = answer(200, page);
  const std::string text = "Title: Fine\n\nMarkdown Content:\nA page of text.\n";

  // The first reads it: the second is not asked.
  ArticleResult r = fetchArticle(services(answer(200, text), good), "https://w.example/a");
  CHECK(r.ok);
  CHECK(asked.size() == 1);
  CHECK(r.article.title == "Fine");
  // A CAPTCHA page (no text, a warning), its limit of requests, a time-out, an error: the second reads it.
  const hal::HttpResponse captcha =
      answer(200, "Title: reuters.com\n\nWarning: This page maybe requiring CAPTCHA, please make sure you are authorized to access this page.\n\nMarkdown Content:\n\n");
  for (const hal::HttpResponse& first : {captcha, answer(429, "RateLimitTriggeredError"), answer(0, "", "timeout"), answer(502, "")}) {
    r = fetchArticle(services(first, good), "https://w.example/a");
    CHECK(r.ok);
    CHECK(asked == std::vector<std::string>{kReaderUrl, kFallbackUrl});
    CHECK(r.article.title == "The \"real\" page");
  }
  // The site refused the first but it sent a page: the longer text of the two.
  const hal::HttpResponse walled = answer(200, "Title: Walled\n\nWarning: Target URL returned error 401: Unauthorized\n\nMarkdown Content:\n" +
                                                   std::string(400, 'x') + "\n");
  r = fetchArticle(services(walled, good), "https://w.example/a");
  CHECK(asked.size() == 2);
  CHECK(r.article.title == "Walled");
  r = fetchArticle(services(answer(200, "Title: Walled\n\nWarning: Target URL returned error 403: Forbidden\n\nMarkdown Content:\nJust a moment...\n"), good),
                   "https://w.example/a");
  CHECK(r.article.title == "The \"real\" page");
  // The second can not read it either: the first's words.
  const hal::HttpResponse none = answer(500, "{\"success\":false,\"error\":\"Error from The API: All conversion methods failed.\"}");
  r = fetchArticle(services(answer(422, "{\"message\":\"Domain 'x.example' could not be resolved\"}"), none), "https://x.example/");
  CHECK_FALSE(r.ok);
  CHECK(asked.size() == 2);
  CHECK(r.error == "Domain 'x.example' could not be resolved");
  r = fetchArticle(services(captcha, answer(200, "{\"success\":true,\"title\":\"reuters.com\",\"content\":\"---\\ntitle: reuters.com\\n---\"}")), "https://x.example/");
  CHECK(r.error == "No text on the page");
  // Without Wi-Fi, or when nothing left the device, the second is not there either; a page too long stays too long.
  CHECK(fetchArticle(services(answer(0, "", "No Wi-Fi: No known network in range"), good), "https://x.example/").error ==
        "No Wi-Fi: No known network in range");
  CHECK(asked.size() == 1);
  fetchArticle(services(answer(0, "", "DNS failed", true), good), "https://x.example/");
  CHECK(asked.size() == 1);
  CHECK(fetchArticle(services(answer(200, std::string(kMaxArticleBytes + 1, 'a')), good), "https://x.example/").error == "The page is too long");
  CHECK(asked.size() == 1);
  // A budget too small for the second: the first's answer.
  CHECK(readPage(services(captcha, good), "https://x.example/", 2000).body == captcha.body);
  CHECK(asked.size() == 1);
}
