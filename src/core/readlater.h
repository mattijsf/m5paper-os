// Read later (task P7-18, docs/apps/reader.md "Articles"): links the phone shared wait here until their pages are read
// and saved as articles in the books folder (apps/reader/articles does that: it needs the network and the reader
// service). Pure logic over strings and one file; unit-tested.
//
// File: /data/readlater.json: the links still to read, and the saved articles nobody has opened yet. Both together are
// the Read tile's badge (and so the count on the sleep screen's board): what there is to read.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pos::readlater {

constexpr int kMaxQueue = 20;      // links waiting: one more pushes the oldest out
constexpr int kMaxTries = 6;       // reads that found no network or no answer, then the link is dropped
constexpr size_t kMaxUrlBytes = 600, kMaxHintChars = 120, kMaxTitleChars = 70;

struct Link {
  std::string url;
  std::string hint;   // words that came with the link (a page title the sharing app put before it), "" for none
  int64_t added = 0;  // the Paper's clock when it arrived (0: not set)
  int tries = 0;
  bool operator==(const Link& o) const { return url == o.url && hint == o.hint && added == o.added && tries == o.tries; }
};
struct State {
  std::vector<Link> queue;         // oldest first
  std::vector<std::string> fresh;  // saved articles never opened: their files inside the books folder ("Articles/x.md")
};

// A shared text that is a link to read: one http(s) address, alone or as the last word after a title ("Title
// https://example.com/a"). Text with no address, with two, or with words after it is not (a note that mentions a link
// stays a note).
bool linkFromText(const std::string& text, std::string& url, std::string& hint);
// "example.com" for "https://www.example.com:8080/a?b": lower case, without "www." and the port. "" if there is none.
std::string hostOf(const std::string& url);
// The file an article is saved as, "Title - host.md": what a FAT name cannot hold is taken out of the title, a " - " in
// it becomes a dash (the reader splits title and author there), long titles are cut. No title: the host alone.
std::string fileName(const std::string& title, const std::string& host);

constexpr const char* kBadgeApp = "read";  // the app whose launcher badge counts what waits here

bool load(State& out);  // false when there is no file or it is unreadable (out is then empty)
bool save(const State& s);  // also sets the badge: links waiting + articles not opened
// Queues a link; false if that address already waits. A full queue drops its oldest link.
bool add(State& s, const std::string& url, const std::string& hint, int64_t now);
// What the relay read calls: every text that is a link is queued and saved. Returns how many were new.
int addShared(const std::vector<std::string>& texts, int64_t now);

}  // namespace pos::readlater
