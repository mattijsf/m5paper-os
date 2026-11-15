// Reader book model: ids, the cache folder, TXT import, page index files. Pure logic (no widgets):
// docs/apps/reader.md "Cache". Everything here may run on the worker thread.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/textblock.h"

namespace pos::reader {

constexpr const char* kBooksDir = "/sd/books";
constexpr const char* kCacheRoot = "/sd/books/.paperos";

struct Chapter {
  std::string title;
};

struct BookMeta {
  std::string id;    // 12 hex chars
  std::string file;  // file name inside /sd/books
  std::string title, author;
  bool epub = false;
  std::vector<Chapter> chapters;
};

// A page of the whole book: chapter and the position inside it.
struct PageRef {
  uint16_t chapter = 0;
  ui::TextPos pos;
  bool operator==(const PageRef& o) const { return chapter == o.chapter && pos == o.pos; }
};

struct Progress {
  int chapter = 0;
  ui::TextPos pos;
  int percent = 0;
  int64_t opened = 0;  // unix seconds, 0 = never
};

bool isBookFile(const std::string& name);   // .txt or .epub, not hidden
bool isEpubFile(const std::string& name);
// First 12 hex chars of a 64-bit FNV-1a hash of "name|size|mtime".
std::string bookId(const std::string& fileName, uint64_t size, int64_t mtime);
std::string cacheDir(const std::string& id);
// "Title - Author.ext" -> title, author; no " - " -> title is the name without extension, author "".
void titleFromFileName(const std::string& file, std::string& title, std::string& author);

// ---- TXT import

// UTF-8 (BOM removed, CRLF -> LF); text that is not valid UTF-8 is read as Latin-1 (Windows-1252 for 0x80-0x9F).
std::string txtToUtf8(const std::string& raw);
struct TxtImport {
  std::string title, author;                // from a "Title:" / "Author:" header (Project Gutenberg), "" if none
  std::vector<std::string> chapters;        // chapter texts in the marked format (ui::parseMarked)
};
// Strips Project Gutenberg boilerplate, joins hard-wrapped lines into paragraphs (blank line = paragraph),
// turns _word_ into italics and short "CHAPTER ..." lines into headings; splits very long texts into parts at
// paragraph boundaries (`maxChapterBytes`).
TxtImport importTxt(const std::string& utf8, size_t maxChapterBytes = 512 * 1024);

// ---- cache files (/sd/books/.paperos/<id>/)

bool writeMeta(const BookMeta& m);
bool readMeta(const std::string& id, BookMeta& m);
bool writeChapter(const std::string& id, int n, const std::string& marked);
bool readChapter(const std::string& id, int n, std::string& marked);
bool writeProgress(const std::string& id, const Progress& p);
bool readProgress(const std::string& id, Progress& p);

// Layout key of a page index: face, column size and line spacing (anything that changes the line breaks).
std::string layoutKey(const ui::TextStyle& s);
bool writeIndex(const std::string& id, const std::string& key, const std::vector<PageRef>& pages);
bool readIndex(const std::string& id, const std::string& key, std::vector<PageRef>& pages);
// Binary form (little endian): "PIX1", u32 count, then per page u16 chapter, u32 paragraph, u32 byte.
std::string encodeIndex(const std::vector<PageRef>& pages);
bool decodeIndex(const std::string& data, std::vector<PageRef>& pages);

// Paginates every chapter (reading each chapter from the cache). `progress(chapterIndex, chapterCount)` runs
// between chapters; return false to cancel (the result is then empty).
std::vector<PageRef> buildIndex(const BookMeta& meta, const ui::TextStyle& style, ui::TextMetrics& metrics,
                                const std::function<bool(int, int)>& progress = {});
// The page that contains the reading position (last page starting at or before it); 0 if the list is empty.
int pageIndexFor(const std::vector<PageRef>& pages, int chapter, ui::TextPos pos);
// Whole-book percentage of a page (0..100), by page number.
int percentOf(int pageIndex, size_t pageCount);

// Opens (and on first use imports) a book. Blocking; run on the worker.
struct OpenResult {
  bool ok = false;
  std::string error;
  BookMeta meta;
  std::vector<PageRef> pages;
  Progress progress;
  bool fromCache = false;  // meta and index were already there
};
OpenResult openBook(const std::string& fileName, const ui::TextStyle& style, ui::TextMetrics& metrics,
                    const std::function<bool(int, int)>& progress = {});

}  // namespace pos::reader
