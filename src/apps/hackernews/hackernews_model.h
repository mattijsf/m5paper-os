// Hacker News (docs/apps/hackernews.md): the story lists and a story's comments from the two public APIs, into what the
// screens show. Pure: no screen; the network only through a transport that is handed in (tests/unit/hackernews_test.cpp).
//
//   order     https://hacker-news.firebaseio.com/v0/<list>.json and /v0/item/<id>.json: the ids of a list in the site's
//             order, a story with its own text and its direct comments in the site's order
//   content   https://hn.algolia.com/api/v1/search: thirty stories in one answer, every comment of a story in one answer
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "hal/net.h"
#include "ui/textblock.h"

namespace pos::hackernews {

enum class Section : uint8_t { Top, New, Ask, Show, Count };

constexpr int kSections = static_cast<int>(Section::Count);
constexpr int kListMax = 30;                 // one page of the site
constexpr int kIdsAsked = 34;                // ids looked up for it: a job advert among them is unknown to the search
constexpr int kCommentsMax = 1000;           // the most the search gives in one answer
constexpr size_t kMaxListBytes = 64 * 1024;  // thirty stories are 8 KB, 500 ids 4.5 KB
constexpr size_t kMaxThreadBytes = 500 * 1024;  // 780 comments are 290 KB (the device takes 512 KB at most)
constexpr int kDepthStep = 16;               // px a reply stands further right than what it answers
constexpr int kDepthMax = 8;                 // deeper replies stay at this depth's margin

const char* sectionName(Section s);  // "Top": the switcher
const char* sectionKey(Section s);   // "top": the stored choice, the name of the saved copy
bool sectionFromKey(const std::string& key, Section& out);

struct Story {
  uint32_t id = 0;
  std::string title, url, by;
  std::string text;  // the story's own text (Ask HN) as marked text (ui::parseMarked), "" if it has none or it was not read
  int points = 0, comments = 0;
  int64_t time = 0;
};

struct Comment {
  uint32_t id = 0, parent = 0;
  std::string by;
  std::string text;   // marked text (ui::parseMarked): a paragraph a line
  int64_t time = 0;
  int depth = 0;      // 0: an answer to the story
  int replies = 0;    // comments below it, at any depth
};

struct Thread {
  std::vector<Comment> comments;  // in reading order: a comment, then its replies, then the next comment of its level
  int total = 0;                  // comments the story has (more than comments.size() beyond kCommentsMax)
};

// ---- addresses

std::string idsUrl(Section s);                               // the ids of a list, in the site's order
std::string storiesUrl(const std::vector<uint32_t>& ids);    // those stories in one answer
std::string itemUrl(uint32_t id);                            // one story: its text, its direct comments in the site's order
std::string commentsUrl(uint32_t storyId);                   // every comment of a story
std::string siteUrl(uint32_t id);                            // the story's page on the site (the QR code when its link is too long)

// ---- answers

// "[50538240,50535230,...]": the first `max` ids. False if it is not a list of numbers.
bool parseIds(const std::string& json, std::vector<uint32_t>& out, int max = kIdsAsked);
// The hits of the story search, in the order of `ids`, at most `max`; an id the search does not know (a job advert) is
// left out.
bool parseStories(const std::string& json, const std::vector<uint32_t>& ids, std::vector<Story>& out, int max = kListMax);
// One item of the first API into `out` (fields it lacks keep their value) and its direct comments in the site's order.
bool parseItem(const std::string& json, Story& out, std::vector<uint32_t>& kids);
// The hits of the comment search as a tree in reading order: answers to the story in the order of `kidOrder` (those it
// does not name after them, oldest first), replies oldest first. A comment whose parent is not among the hits is left out.
bool parseComments(const std::string& json, uint32_t storyId, const std::vector<uint32_t>& kidOrder, Thread& out);

// A comment's or story's HTML as marked text (ui::parseMarked): a paragraph a line, the lines of a code block marked as
// continuing (0x06); tags gone, character references decoded, only what the fonts can draw.
std::string markedText(const std::string& html);

// ---- reads (blocking: on the worker)

using Transport = std::function<hal::HttpResponse(const hal::HttpRequest&)>;

struct ListResult {
  bool ok = false;
  std::string error;  // one line for the user
  std::vector<Story> stories;
};
// Two requests: the ids, then the stories.
ListResult fetchList(const Transport& send, Section s);

struct ThreadResult {
  bool ok = false;
  std::string error;
  Story story;  // as read just now (points, comment count, text)
  Thread thread;
};
// Two requests: the story (for its text and the order of its comments) and the comments, first the one to the service the
// last request went to (the connection to it is still open). `known` is the story as the list has it: what the item
// lacks stays.
ThreadResult fetchThread(const Transport& send, const Story& known);

// ---- text

// "now", "5 min", "3 h", "2 d": how old. "" if either time is unknown.
std::string ageText(int64_t time, int64_t now);
// "312 points · 141 comments · 5 h · example.com": the line under a title in the list.
std::string metaLine(const Story& s, int64_t now);
// "312 points · by pg · 5 h · example.com": the line under the title of the story's own screen.
std::string storyLine(const Story& s, int64_t now);

// The story's text and its comments as paragraphs for a ui::TextBlock: per comment a byline ("pg · 3 h · 12 replies") and
// its paragraphs, moved right by its depth.
struct ThreadText {
  std::vector<ui::Paragraph> paras;
  std::vector<int> topLevel;   // the paragraph each answer to the story starts at (its byline)
  std::vector<uint32_t> ids;   // every comment in reading order
  std::vector<int> firstPara;  // and the paragraph it starts at (its byline)
};
ThreadText threadText(const Story& story, const Thread& thread, int64_t now);
// The answer to the story that starts after paragraph `after`: its paragraph, -1 if there is none.
int nextTopLevel(const ThreadText& t, int after);

// A place in a thread that is still right after the thread was read again (comments came, the order changed): the
// comment it lies in (0: before the first comment, in the story's own text), and how far into that comment.
struct Anchor {
  uint32_t comment = 0;
  int para = 0;  // paragraphs after the comment's byline (after the start of the story's text for comment 0)
  int byte = 0;
};
Anchor anchorOf(const ThreadText& t, ui::TextPos pos);
// The start of the text if the comment is no longer there.
ui::TextPos posOf(const ThreadText& t, const Anchor& a);

// ---- the article a story links to, as text (a reader service: docs/apps/hackernews.md "The article")

constexpr const char* kReaderUrl = "https://r.jina.ai/";          // the reader service (Jina Reader)
constexpr const char* kFallbackUrl = "https://markdown.new/";     // the second one, when the first can not read a page
constexpr size_t kMaxArticleBytes = 400 * 1024;  // a long article is 50 KB of text

struct Article {
  std::string title;  // the page's title, "" if the service named none
  std::string text;   // marked text (ui::parseMarked): the title as a heading when the text does not start with one
};

// The request for the text of the page at `url`: its address goes to the service, nothing else.
hal::HttpRequest articleRequest(const std::string& url);
// The same for the second service (markdown.new).
hal::HttpRequest fallbackRequest(const std::string& url);
// The second service's answer (JSON: success, title, content) in the first one's format, "" if it holds no page.
std::string fallbackAnswer(const std::string& json, const std::string& url);
// Whether the second service is worth asking after the first answered `r`: it refused or failed (a status, a time-out, its
// limit of requests), found no text, or says the site refused it ("Warning: Target URL returned error 403", a CAPTCHA). Not
// when nothing left the device (no Wi-Fi) or the page is too long.
bool needsFallback(const hal::HttpResponse& r);
// The page at `url` as text, in the first service's format: the first service, then the second when needsFallback (of two
// texts the longer). The first service's answer when the second has nothing better: its error is the one to show.
// `budgetMs` > 0: both requests together take at most that long.
hal::HttpResponse readPage(const Transport& send, const std::string& url, uint32_t budgetMs = 0);
// A service's answer ("Title: ...", "Markdown Content:", then the page as Markdown, maybe after a YAML front matter) into
// `out`. False if it holds no text.
bool parseArticle(const std::string& body, Article& out);
// Markdown as marked text: headings (# and ## are level 2, deeper ones 3), paragraphs, list items with a bullet, code blocks and
// table rows as lines that stay lines, **bold**; the marks of emphasis, inline code and links are dropped. `web`: a page
// from a reader service, so what its source left (a lone tag, "{{ template }}") goes and a short paragraph is kept once
// (page furniture); not for a Markdown file of the user's (a poem's refrain stays).
std::string articleMarked(const std::string& markdown, bool web = false);

struct ArticleResult {
  bool ok = false;
  std::string error;
  Article article;
};
ArticleResult fetchArticle(const Transport& send, const std::string& url);

// ---- the saved copy of a list (a few KB: /data/cache/hackernews/<section>)

std::string packList(int64_t fetched, const std::vector<Story>& stories);  // without the stories' own text
bool unpackList(const std::string& saved, int64_t& fetched, std::vector<Story>& stories);

}  // namespace pos::hackernews
