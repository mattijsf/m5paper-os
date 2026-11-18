// The Read app's first screen: the books on the card (docs/apps/reader.md "Library").
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "apps/reader/book.h"
#include "core/app.h"
#include "ui/widgets.h"

namespace pos::reader {

class LibraryScreen : public Screen {
 public:
  // `open(file)` is called when a book is tapped (the app pushes the page screen).
  // `covered`: a book's page is pushed over the library at once (the app resumes into the open book). The scan and the
  // cover decode then wait until the library is shown: on the one worker they held the book's open back (2.45 s instead
  // of 0.25 s from the Read tile to the page, device 2026-11-16).
  explicit LibraryScreen(std::function<void(const std::string&)> open, bool covered = false) : open_(std::move(open)), covered_(covered) {}
  void build(ui::Column& root) override;
  void onShow() override;
  std::string debugState() const override;

 private:
  void refresh();
  void rebuild();
  void fill(int page);
  void confirmRemove(const std::string& file, const std::string& title);
  void fetchWaiting();

  std::function<void(const std::string&)> open_;
  std::shared_ptr<Canvas> thumb_;  // the card book's cover, decoded on the worker
  std::string thumbPath_;          // the file thumb_ was decoded from
  std::vector<LibraryEntry> entries_;  // all books, most recent first
  std::vector<LibraryEntry> others_;   // the list rows: entries_ without the card's book
  int firstRows_ = 7;
  bool loaded_ = false;
  bool covered_ = false;  // skip the scan of the first onShow()
  int waiting_ = 0;          // links to read later that are not read yet
  bool fetching_ = false, triedFetch_ = false;
  ui::Column* holder_ = nullptr;
  ui::Pager* pager_ = nullptr;
  ui::Column* list_ = nullptr;
  ui::Column* card_ = nullptr;
  ui::Widget* cover_ = nullptr;
  std::vector<ui::ListItem*> rows_;
  int page_ = 0;
};

}  // namespace pos::reader
