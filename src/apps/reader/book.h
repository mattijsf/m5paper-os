// Reader book model: ids, the cache folder, TXT import, page index files. Pure logic (no widgets):
// docs/apps/reader.md "Cache". Everything here may run on the worker thread.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "apps/reader/epub.h"
#include "core/zip.h"
#include "ui/textblock.h"

namespace pos::reader {

constexpr const char* kBooksDir = "/sd/books";
constexpr const char* kCacheRoot = "/sd/books/.paperos";

struct Chapter {
  std::string title;
  std::string href;  // EPUB: the chapter's XHTML document inside the zip; "" for TXT
  bool toc = false;  // the title comes from the book's table of contents (the footer shows it; otherwise the book title)
  size_t from = 0, to = 0;  // EPUB: the chapter is this byte range of the converted document (a document with several contents
                            // entries, or of more than 512 KB, makes several chapters); to = 0: the whole document
};

struct BookMeta {
  std::string id;    // 12 hex chars
  std::string file;  // file name inside /sd/books
  std::string title, author;
  bool epub = false;
  std::string cover;  // EPUB: path of the cover image inside the zip, "" if none (extractCover() copies it to the cache folder)
  std::vector<Chapter> chapters;
  std::vector<TocEntry> tocEntries;  // EPUB, while importing: the contents with anchors (not stored)
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

// ---- EPUB

// Reads an EPUB's structure into a BookMeta (title, author, cover, chapter list with hrefs); `meta.file` / `meta.id`
// are left to the caller. Chapters without a table-of-contents title are named "Chapter N" until their text is read.
bool epubMeta(const std::string& virtualPath, BookMeta& meta, std::string& error);

// Converts every chapter document of an EPUB to the marked text, in memory (nothing is written to the card: writing is the
// slow part on the device, and a chapter is cheap to convert again). Replaces meta.chapters: documents with no text are
// dropped, very long ones (over 512 KB of text) are split into parts, a chapter without a contents title gets the first
// heading of its text. `texts[i]` is the text of chapter i. `step(i, n)` before document i of n; false cancels (error "Cancelled").
bool convertEpub(const std::string& virtualPath, BookMeta& meta, std::vector<std::string>& texts, std::string& error,
                 const std::function<bool(int, int)>& step = {});

// Marked text of a chapter on demand: from the cache file for a TXT book, converted from the EPUB for an EPUB (the zip stays
// open between calls and the last document is remembered, so the parts of one document cost one conversion).
class ChapterLoader {
 public:
  explicit ChapterLoader(const BookMeta& meta) : meta_(meta) {}
  bool load(int chapter, std::string& marked);

 private:
  const BookMeta& meta_;
  zip::Archive zip_;
  bool zipTried_ = false, zipOk_ = false;
  std::string docHref_, doc_;
};

// The cover image: copies it from the EPUB to the cache folder as cover.jpg / cover.png (M5GFX decodes both when drawn).
// Slow on the card (it is a write): the reader does it in the background after the book is open.
bool extractCover(const BookMeta& meta);
// Real path of the extracted cover ("/sd/books/.paperos/<id>/cover.jpg"), "" if the book has none or it is not extracted yet.
std::string coverFile(const BookMeta& meta);

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

// Remembers the width of recently measured (face, text) pairs: a book repeats its words, and measuring is the slow part of
// pagination. A fixed table (one allocation, no per-entry heap blocks: the small internal heap of the device stays free);
// entries are found by a 64-bit hash of face and text, a colliding entry just replaces the old one. Wraps another
// TextMetrics; use one per indexing run (not shared between threads).
class CachedMetrics : public ui::TextMetrics {
 public:
  explicit CachedMetrics(ui::TextMetrics& inner, size_t slots = 16384) : inner_(inner), table_(slots) {}
  int width(const std::string& utf8, ui::Type type, bool bold, bool italic) override;
  int lineHeight(ui::Type type) override { return inner_.lineHeight(type); }
  uint32_t hits() const { return hits_; }
  uint32_t misses() const { return misses_; }
  uint32_t measureMs() const { return measureUs_ / 1000; }

 private:
  struct Slot {
    uint64_t key = 0;  // 0 = empty
    int32_t width = 0;
  };
  ui::TextMetrics& inner_;
  std::vector<Slot> table_;
  uint32_t hits_ = 0, misses_ = 0, measureUs_ = 0;
};

// Paginates every chapter: from `texts` (chapter texts already in memory) or, when that is null, through a ChapterLoader.
// `progress(chapterIndex, chapterCount)` runs between chapters and now and then inside one; return false to cancel (the
// result is then empty).
std::vector<PageRef> buildIndex(const BookMeta& meta, const ui::TextStyle& style, ui::TextMetrics& metrics,
                                const std::function<bool(int, int)>& progress = {}, const std::vector<std::string>* texts = nullptr);
// The page that contains the reading position (last page starting at or before it); 0 if the list is empty.
int pageIndexFor(const std::vector<PageRef>& pages, int chapter, ui::TextPos pos);
// Whole-book percentage of a page (0..100), by page number.
int percentOf(int pageIndex, size_t pageCount);

// ---- library

struct LibraryEntry {
  std::string file, id, title, author;
  bool epub = false;
  int percent = 0;     // reading progress
  int64_t opened = 0;  // unix seconds of the last time it was opened, 0 = never
  std::string cover;   // real path of the extracted cover image, "" if none (yet)
};
// Books in /sd/books (.epub, .txt) with title and author (from the cache once a book was opened, else from the
// file name) and reading progress. Blocking: run on the worker. Sorted by sortLibrary().
std::vector<LibraryEntry> scanLibrary();
// Most recently opened first, then never-opened books by title.
void sortLibrary(std::vector<LibraryEntry>& entries);

// Opens (and on first use imports) a book. Blocking; run on the worker.
// Milliseconds spent per kind of work while a book is converted and paginated (BookTiming::global() is filled by the
// functions below; the worker runs one job at a time).
struct Timing {
  uint32_t zipMs = 0;     // reading and inflating the zip entries
  uint32_t convertMs = 0; // XHTML -> marked text
  uint32_t writeMs = 0;   // chapter files to the card
  uint32_t readMs = 0;    // chapter files back from the card
  uint32_t parseMs = 0;   // marked text -> paragraphs
  uint32_t layoutMs = 0;  // pagination
  uint32_t measureMs = 0; // of which: measuring text (misses of the width cache)
  uint32_t structureMs = 0;  // reading the EPUB structure (container, package, contents)
  uint32_t coverMs = 0;      // extracting the cover
  uint32_t metaMs = 0;       // writing meta.json
  uint32_t hits = 0, misses = 0;  // width cache
};
Timing& timing();  // the running totals; openBook() resets and copies them into the result

struct OpenResult {
  bool ok = false;
  std::string error;
  BookMeta meta;
  std::vector<PageRef> pages;
  Progress progress;
  bool fromCache = false;  // meta and index were already there
  uint32_t importMs = 0, indexMs = 0;  // time spent converting the book and paginating it (0 when cached)
  Timing timing;                       // where the time went (diagnostics, logged by the reader)
};
// `step(done, total)` reports progress (importing the chapters, then paginating them); return false to cancel
// (the result is then not ok, error "Cancelled").
OpenResult openBook(const std::string& fileName, const ui::TextStyle& style, ui::TextMetrics& metrics,
                    const std::function<bool(int, int)>& step = {});

}  // namespace pos::reader
