// Read later (task P7-18, docs/apps/reader.md "Articles"): the links the phone shared (core/readlater.h) become
// articles in the books folder, /sd/books/Articles/<title> - <host>.md, which the library lists and the page screen
// opens like any book. The page is read through the reader service the Hacker News app uses (D34: the link's
// address is what leaves the device) and saved as the service answers: its title lines, then Markdown.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "hal/net.h"

namespace pos::reader {

constexpr const char* kArticlesFolder = "Articles";

using Transport = std::function<hal::HttpResponse(const hal::HttpRequest&)>;

struct ArticleFetch {
  int saved = 0;     // articles written to the card
  int dropped = 0;   // links given up: the page has no text, the service refused it, or kMaxTries reads found no answer
  int left = 0;      // links still waiting
  std::string note;  // the last failure for the user, "example.com: No text on the page"; "" when all went well
};
// Reads every waiting link and saves what answers. Blocking: worker thread. Stops at the first read that found no
// network (the others would not either). The Read tile's badge follows (readlater::save). `budgetMs` > 0 (the scheduled refresh, which
// must be off again within its limit): no read starts with less than 5 s of it left, and none waits longer than
// what is left.
ArticleFetch fetchArticles(const Transport& send, int64_t now, uint32_t budgetMs = 0);
// Links waiting to be read (one small file; no network).
int waitingLinks();

// `file` (a book's path inside the books folder) is a saved article.
bool isArticleFile(const std::string& file);
// The article was opened: it is no longer new (the badge follows).
void articleOpened(const std::string& file);
// Deletes an article and the reader's cache of it. False if `file` is no article or the delete failed.
bool removeArticle(const std::string& file);

}  // namespace pos::reader
