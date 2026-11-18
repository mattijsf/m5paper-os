// Read later (task P7-18): the link rule, the queue (core/readlater), saving articles and the reader's Markdown import
// (apps/reader/articles, apps/reader/book).
#include <doctest.h>

#include <cstdlib>
#include <string>
#include <unistd.h>
#include <vector>

#include "apps/reader/articles.h"
#include "apps/reader/book.h"
#include "core/fs.h"
#include "core/readlater.h"
#include "core/store.h"

using namespace pos;
using namespace pos::readlater;

namespace {

struct Roots {
  std::string dir;
  Roots() {
    char tmpl[] = "/tmp/pos-readlater-XXXXXX";
    dir = mkdtemp(tmpl);
    fs::setRoots(dir + "/sd", dir + "/data");
    fs::mkdirs("/data");
    fs::mkdirs("/sd/books");
  }
  ~Roots() {
    std::string cmd = "rm -rf '" + dir + "'";
    (void)!std::system(cmd.c_str());
  }
};

const int64_t kNow = 1768480440;  // 2026-01-15T12:34:00Z

const char* kPage = "Title: The page: a \"test\" - part 1\n\nURL Source: https://www.example.com/a\n\nMarkdown Content:\n"
                    "# The page\n\nFirst **bold** paragraph.\n\n## More\n\n- one\n- two\n";

hal::HttpResponse answer(int status, const std::string& body, const std::string& error = "") {
  hal::HttpResponse r;
  r.status = status;
  r.body = body;
  r.error = error;
  return r;
}

}  // namespace

TEST_CASE("read later: a shared text is a link when it is one address, alone or after a title") {
  std::string url, hint;
  REQUIRE(linkFromText("https://example.com/a?b=1#c", url, hint));
  CHECK(url == "https://example.com/a?b=1#c");
  CHECK(hint.empty());
  REQUIRE(linkFromText("  A fine page\nhttp://Example.com/x \n", url, hint));
  CHECK(url == "http://Example.com/x");
  CHECK(hint == "A fine page");
  REQUIRE(linkFromText("HTTPS://EXAMPLE.COM", url, hint));
  // Not a link to read: no address, words after it, two addresses, another scheme, no host, far too long.
  CHECK_FALSE(linkFromText("Pick up bread", url, hint));
  CHECK_FALSE(linkFromText("", url, hint));
  CHECK_FALSE(linkFromText("see https://example.com for details", url, hint));
  CHECK_FALSE(linkFromText("https://a.example https://b.example", url, hint));
  CHECK_FALSE(linkFromText("ftp://example.com/file", url, hint));
  CHECK_FALSE(linkFromText("https://", url, hint));
  CHECK_FALSE(linkFromText("https:///path", url, hint));
  CHECK_FALSE(linkFromText("https://example.com/" + std::string(kMaxUrlBytes, 'a'), url, hint));
  CHECK(url == "HTTPS://EXAMPLE.COM");  // untouched by a text that is none
}

TEST_CASE("read later: hosts and file names") {
  CHECK(hostOf("https://www.Example.com:8443/a/b?c") == "example.com");
  CHECK(hostOf("http://user@blog.example.net") == "blog.example.net");
  CHECK(hostOf("no address") == "");
  CHECK(fileName("A plain title", "example.com") == "A plain title - example.com.md");
  // What a FAT name cannot hold goes; " - " would be read as title and author.
  CHECK(fileName("The page: a \"test\" - part 1/2?", "example.com") == "The page a test \xE2\x80\x93 part 1 2 - example.com.md");
  CHECK(fileName("Ends in dots...", "example.com") == "Ends in dots - example.com.md");
  CHECK(fileName(".hidden", "example.com") == "hidden - example.com.md");
  CHECK(fileName("", "example.com") == "example.com.md");
  CHECK(fileName("", "") == "Article.md");
  CHECK(fileName("Caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D", "example.com") == "Caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D - example.com.md");
  const std::string name = fileName(std::string(300, 'x'), "example.com");
  CHECK(name.size() == kMaxTitleChars + std::string(" - example.com.md").size());
  // The reader reads title and author back from the name.
  std::string title, author;
  reader::titleFromFileName(fileName("One - two - three", "example.com"), title, author);
  CHECK(title == "One \xE2\x80\x93 two \xE2\x80\x93 three");
  CHECK(author == "example.com");
}

TEST_CASE("read later: the queue is saved, holds an address once and drops its oldest when full") {
  Roots roots;
  State s;
  CHECK_FALSE(load(s));
  CHECK(add(s, "https://example.com/1", "One", kNow));
  CHECK_FALSE(add(s, "https://example.com/1", "again", kNow + 5));
  CHECK(add(s, "https://example.com/2", "", kNow + 9));
  s.queue[1].tries = 2;
  s.fresh = {"Articles/One - example.com.md"};
  REQUIRE(save(s));
  State back;
  REQUIRE(load(back));
  CHECK(back.queue == s.queue);
  CHECK(back.fresh == s.fresh);
  for (int i = 0; i < kMaxQueue; ++i) add(back, "https://example.com/n" + std::to_string(i), "", kNow);
  CHECK(static_cast<int>(back.queue.size()) == kMaxQueue);
  CHECK(back.queue.front().url == "https://example.com/n0");

  // What the relay read hands over: only the texts that are links, each once.
  CHECK(addShared({"Pick up bread", "Two https://example.com/2", "New https://example.com/3"}, kNow) == 1);
  REQUIRE(load(back));
  CHECK(back.queue.size() == 3);
  CHECK(back.queue.back().hint == "New");
  CHECK(appBadge("read") == 4);  // the Read tile's badge: three links and the article nobody opened
}

TEST_CASE("read later: a link that answers is saved as an article; one that never will is dropped, one without network waits") {
  Roots roots;
  State s;
  add(s, "https://www.example.com/a", "Shared title", kNow);
  add(s, "https://empty.example/b", "", kNow);
  add(s, "https://refused.example/c", "", kNow);
  add(s, "https://slow.example/d", "", kNow);
  REQUIRE(save(s));
  CHECK(reader::waitingLinks() == 4);
  std::vector<std::string> asked;
  const reader::Transport send = [&](const hal::HttpRequest& req) {
    asked.push_back(req.body);
    if (req.body.find("www.example.com") != std::string::npos) return answer(200, kPage);
    if (req.body.find("empty.example") != std::string::npos) return answer(200, "Title: Empty\n\nMarkdown Content:\n\n");
    if (req.body.find("refused.example") != std::string::npos) return answer(422, "{}");
    return answer(503, "busy");
  };
  reader::ArticleFetch f = reader::fetchArticles(send, kNow);
  CHECK(asked.size() == 4);
  CHECK(f.saved == 1);
  CHECK(f.dropped == 2);  // no text, refused
  CHECK(f.left == 1);     // the busy service is asked again
  CHECK(f.note == "slow.example: The reader answered 503");
  const std::string file = "Articles/The page a test \xE2\x80\x93 part 1 - example.com.md";
  std::string saved;
  REQUIRE(fs::readFile("/sd/books/" + file, saved));
  CHECK(saved == kPage);  // as the service answered
  REQUIRE(load(s));
  REQUIRE(s.queue.size() == 1);
  CHECK(s.queue[0].tries == 1);
  CHECK(s.fresh == std::vector<std::string>{file});
  CHECK(appBadge("read") == 2);  // to read: the article and the link that waits

  // The same page shared again: a second file, not the first one replaced.
  add(s, "https://www.example.com/a", "", kNow);
  s.queue.erase(s.queue.begin());
  REQUIRE(save(s));
  f = reader::fetchArticles(send, kNow);
  CHECK(f.saved == 1);
  CHECK(fs::exists("/sd/books/Articles/The page a test \xE2\x80\x93 part 1 - example.com (2).md"));
  CHECK(appBadge("read") == 2);

  // No network: the first read says so, the others are not tried, nothing is dropped until kMaxTries.
  REQUIRE(load(s));
  CHECK(s.queue.empty());
  add(s, "https://one.example/", "", kNow);
  add(s, "https://two.example/", "", kNow);
  s.queue[0].tries = kMaxTries - 1;
  s.fresh.clear();
  REQUIRE(save(s));
  asked.clear();
  f = reader::fetchArticles([&](const hal::HttpRequest& req) {
    asked.push_back(req.body);
    return answer(0, "", "No Wi-Fi: No known network in range");
  }, kNow);
  CHECK(asked.size() == 1);
  CHECK(f.dropped == 1);  // its last try
  CHECK(f.left == 1);
  CHECK(f.note == "one.example: No Wi-Fi: No known network in range");

  // Opened: no longer new. Removed: the file and the reader's cache of it go; a book is never removed this way.
  REQUIRE(load(s));
  s.fresh = {file};
  REQUIRE(save(s));
  CHECK(appBadge("read") == 2);  // with the link that still waits
  reader::articleOpened(file);
  CHECK(appBadge("read") == 1);
  CHECK(reader::isArticleFile(file));
  CHECK_FALSE(reader::isArticleFile("Articles/notes.txt"));
  CHECK_FALSE(reader::isArticleFile("Classics/Alice.md"));
  fs::writeFile("/sd/books/Alice.md", "# Alice\n\nText.\n");
  CHECK_FALSE(reader::removeArticle("Alice.md"));
  CHECK_FALSE(reader::removeArticle("Articles/../Alice.md"));
  CHECK(reader::removeArticle(file));
  CHECK_FALSE(fs::exists("/sd/books/" + file));
  CHECK(fs::exists("/sd/books/Alice.md"));
}

TEST_CASE("read later: the reader opens Markdown, and the library lists new articles first") {
  using namespace pos::reader;
  CHECK(isBookFile("Page - example.com.md"));
  CHECK(isMarkdownFile("NOTES.MD"));
  CHECK_FALSE(isMarkdownFile(".hidden.md"));
  CHECK_FALSE(isEpubFile("Page.md"));
  const TxtImport page = importMarkdown(kPage);
  CHECK(page.title == "The page: a \"test\" - part 1");
  REQUIRE(page.chapters.size() == 1);
  CHECK(page.chapters[0].find("\x05" "2The page\n") == 0);  // its own heading, not the title once more
  CHECK(page.chapters[0].find("First \x01" "bold\x02 paragraph.") != std::string::npos);
  const TxtImport plain = importMarkdown("Just a note.\n\nSecond paragraph.");  // any Markdown file on the card
  CHECK(plain.title.empty());
  REQUIRE(plain.chapters.size() == 1);
  CHECK(importMarkdown("").chapters.empty());

  std::vector<LibraryEntry> entries(5);
  entries[0].title = "Zebra book";
  entries[1].title = "Old article";
  entries[1].article = true;
  entries[1].added = 100;
  entries[2].title = "Being read";
  entries[2].opened = 50;
  entries[3].title = "New article";
  entries[3].article = true;
  entries[3].added = 200;
  entries[4].title = "Alpha book";
  sortLibrary(entries);
  CHECK(entries[0].title == "Being read");
  CHECK(entries[1].title == "New article");
  CHECK(entries[2].title == "Old article");
  CHECK(entries[3].title == "Alpha book");
  CHECK(entries[4].title == "Zebra book");
}
