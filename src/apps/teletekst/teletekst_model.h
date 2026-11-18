// Teletekst: a page of NOS Teletekst as a grid of 25 rows of 40 cells, read from the JSON the NOS site itself uses
// (docs/apps/teletekst.md). Pure: no screen, no network; tests/unit/teletekst_test.cpp.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pos::teletekst {

constexpr int kCols = 40, kRows = 25;
constexpr int kFirstPage = 100, kLastPage = 899;
constexpr const char* kBaseUrl = "https://teletekst-data.nos.nl/json/";
constexpr size_t kMaxBodyBytes = 64 * 1024;  // a page is 3-9 KB

// The eight teletext colours.
enum Colour : uint8_t { Black, Red, Green, Yellow, Blue, Magenta, Cyan, White };

struct Cell {
  uint16_t ch = ' ';     // the character (Unicode); for a mosaic its six blocks: bit 0 top left, 1 top right, 2 middle left,
                         // 3 middle right, 4 bottom left, 5 bottom right
  uint8_t fg = White;
  uint8_t bg = Black;
  bool mosaic = false;
  uint16_t link = 0;     // the page a tap on this cell opens, 0 = none
  uint8_t linkSub = 0;   // its subpage, 0 = the first
};

// A page number with a subpage: 104, or 702 with subpage 2 ("702-2" in an address, "702/2" on a page).
struct PageRef {
  int page = 0;
  int sub = 0;  // 0: no subpage named (the first)
  bool valid() const { return page >= kFirstPage && page <= kLastPage; }
  bool operator==(const PageRef& o) const { return page == o.page && sub == o.sub; }
  bool operator!=(const PageRef& o) const { return !(*this == o); }
  // "104" or "702-2": what the address ends in, and the name of the saved copy.
  std::string id() const;
};

struct FastLink {
  std::string title;
  PageRef to;
};

// 8 KB: keep it on the heap (std::make_shared<Page>()), never as a local of code that runs on the device's UI thread
// (its stack is 8 KB).
struct Page {
  PageRef self;                        // what was asked for; sub is 1 when the page turned out to have subpages
  PageRef prev, next, prevSub, nextSub;  // invalid: there is none
  std::vector<FastLink> fast;          // the four coloured links of the bottom row (at most 4)
  Cell cells[kRows][kCols];
  bool hasSubpages() const { return prevSub.valid() || nextSub.valid(); }
};

// "104", "#104", "702-2", "702/2", "#702/2". False for anything else and for a number outside 100..899.
bool parseRef(const std::string& text, PageRef& out);
std::string pageUrl(const PageRef& ref);

// Fills `out` from the JSON of one page (`asked` is the page that was requested: the JSON does not name itself).
// False if it is not that JSON or has no content.
bool parsePage(const std::string& json, const PageRef& asked, Page& out);
// The page's text with its markup (spans with colour classes, links, character references) into the cells.
void parseContent(const std::string& html, Page& out);

// The link a touch on (row, col) means: a link in that cell, else the link of that row nearest to the column, else the
// nearest one in the row above or below (the rows are 26 px high: a finger is not that exact). Invalid if none.
PageRef linkNear(const Page& page, int row, int col);
// The row's text as UTF-8, mosaics as spaces (tests, the log).
std::string rowText(const Page& page, int row);

// A saved copy: the time it was read on the first line, the JSON after it.
std::string packSaved(int64_t fetched, const std::string& json);
bool unpackSaved(const std::string& saved, int64_t& fetched, std::string& json);

}  // namespace pos::teletekst
