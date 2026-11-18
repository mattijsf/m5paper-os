#include "apps/reader/articles.h"

#include <algorithm>
#include <chrono>

#include "apps/hackernews/hackernews_model.h"
#include "apps/reader/book.h"
#include "core/fs.h"
#include "core/log.h"
#include "core/readlater.h"
#include "core/store.h"
#include "core/strings.h"

namespace pos::reader {

namespace {

std::string folder() { return std::string(kBooksDir) + "/" + kArticlesFolder; }

// Drops what is no longer on the card (removed over Wi-Fi, the card changed); readlater::save() sets the badge.
void settle(readlater::State& s) {
  s.fresh.erase(std::remove_if(s.fresh.begin(), s.fresh.end(), [](const std::string& f) { return !fs::exists(std::string(kBooksDir) + "/" + f); }),
                s.fresh.end());
}

// A name in the articles folder that is not taken: "Title - host.md", "Title - host (2).md", ...
std::string freeName(const std::string& name) {
  if (!fs::exists(folder() + "/" + name)) return name;
  const std::string stem = name.substr(0, name.size() - 3);
  for (int n = 2; n < 100; ++n) {
    const std::string next = stem + " (" + std::to_string(n) + ").md";
    if (!fs::exists(folder() + "/" + next)) return next;
  }
  return name;  // a hundred copies of one page: the oldest is replaced
}

}  // namespace

bool isArticleFile(const std::string& file) {
  return startsWith(file, std::string(kArticlesFolder) + "/") && endsWith(lowerAscii(file), ".md");
}

int waitingLinks() {
  readlater::State s;
  readlater::load(s);
  return static_cast<int>(s.queue.size());
}

ArticleFetch fetchArticles(const Transport& send, int64_t now, uint32_t budgetMs) {
  ArticleFetch out;
  const auto t0 = std::chrono::steady_clock::now();
  readlater::State s;
  if (!readlater::load(s) || s.queue.empty()) return out;
  const std::vector<readlater::Link> todo = s.queue;
  bool offline = false;
  for (const readlater::Link& link : todo) {
    if (offline) break;
    const std::string host = readlater::hostOf(link.url);
    hal::HttpRequest request = hackernews::articleRequest(link.url);
    if (budgetMs > 0) {
      const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (spent + 5000 > static_cast<long long>(budgetMs)) break;
      request.timeoutMs = std::min<uint32_t>(request.timeoutMs, budgetMs - static_cast<uint32_t>(spent));
    }
    const hal::HttpResponse r = send(request);
    const auto queued = std::find_if(s.queue.begin(), s.queue.end(), [&](const readlater::Link& l) { return l.url == link.url; });
    hackernews::Article article;
    std::string why;
    bool gone = false;  // never to be read: do not try again
    if (r.status == 200 && r.error.empty()) {
      if (r.body.size() > hackernews::kMaxArticleBytes) {
        why = "The page is too long";
        gone = true;
      } else if (!hackernews::parseArticle(r.body, article)) {
        why = "No text on the page";
        gone = true;
      }
    } else if (r.status == 0) {
      why = r.error.empty() ? std::string("No connection") : r.error;
      offline = true;
    } else {
      why = "The reader answered " + std::to_string(r.status);
      gone = r.status >= 400 && r.status < 500 && r.status != 408 && r.status != 429;  // the page is refused; 5xx and limits pass
    }
    if (why.empty()) {
      const std::string title = !article.title.empty() ? article.title : link.hint;
      fs::mkdirs(folder());
      const std::string name = freeName(readlater::fileName(title, host));
      if (fs::writeFile(folder() + "/" + name, r.body)) {
        s.fresh.push_back(std::string(kArticlesFolder) + "/" + name);
        s.queue.erase(queued);
        ++out.saved;
        POS_LOG("read", "article saved: %s, %u bytes", host.c_str(), static_cast<unsigned>(r.body.size()));
        continue;
      }
      why = "Cannot write to the card";
    }
    out.note = (host.empty() ? std::string("Link") : host) + ": " + why;
    POS_LOG("read", "article not read: %s", out.note.c_str());
    if (gone || ++queued->tries >= readlater::kMaxTries) {
      s.queue.erase(queued);
      ++out.dropped;
    }
  }
  out.left = static_cast<int>(s.queue.size());
  settle(s);
  readlater::save(s);
  return out;
}

void articleOpened(const std::string& file) {
  if (!isArticleFile(file)) return;
  readlater::State s;
  if (!readlater::load(s)) return;
  const size_t before = s.fresh.size();
  s.fresh.erase(std::remove(s.fresh.begin(), s.fresh.end(), file), s.fresh.end());
  settle(s);
  if (s.fresh.size() != before) readlater::save(s);
}

bool removeArticle(const std::string& file) {
  if (!isArticleFile(file) || file.find("..") != std::string::npos) return false;
  const std::string path = std::string(kBooksDir) + "/" + file;
  uint64_t size = 0;
  int64_t mtime = 0;
  if (!fs::fileInfo(path, size, mtime)) return false;
  const std::string cache = cacheDir(bookId(file, size, mtime));
  if (!fs::removeAll(path)) return false;
  if (fs::exists(cache)) fs::removeAll(cache);
  readlater::State s;
  if (readlater::load(s)) {
    const size_t before = s.fresh.size();
    settle(s);
    if (s.fresh.size() != before) readlater::save(s);
  }
  return true;
}

}  // namespace pos::reader
