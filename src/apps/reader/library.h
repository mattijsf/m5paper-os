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
  explicit LibraryScreen(std::function<void(const std::string&)> open) : open_(std::move(open)) {}
  void build(ui::Column& root) override;
  void onShow() override;
  std::string debugState() const override;

 private:
  void refresh();
  void rebuild();
  void fill(int page);

  std::function<void(const std::string&)> open_;
  std::shared_ptr<Canvas> thumb_;  // the card book's cover, decoded on the worker
  std::vector<LibraryEntry> entries_;  // all books, most recent first
  std::vector<LibraryEntry> others_;   // the list rows: entries_ without the card's book
  int firstRows_ = 7;
  bool loaded_ = false;
  ui::Column* holder_ = nullptr;
  ui::Pager* pager_ = nullptr;
  ui::Column* list_ = nullptr;
  ui::Column* card_ = nullptr;
  ui::Widget* cover_ = nullptr;
  std::vector<ui::ListItem*> rows_;
  int page_ = 0;
};

}  // namespace pos::reader
