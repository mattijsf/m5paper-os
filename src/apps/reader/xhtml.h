// XHTML chapter -> the reader's marked text (docs/apps/reader.md "Cache"): one paragraph per line, style marks as
// control bytes (0x01/0x02 bold, 0x03/0x04 italic, 0x05 + digit heading). Block elements start paragraphs, white
// space collapses, images become "[image]", lists get bullets or numbers, tables are flattened to lines, CSS and
// scripts are ignored. Forgiving: broken markup never crashes, it just gives less text.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pos::reader {

struct XhtmlResult {
  std::string marked;        // paragraphs, each ending in '\n'
  std::string firstHeading;  // plain text of the first heading, "" if none (a chapter title fallback)
  // id / name attributes: the byte offset in `marked` of the paragraph that follows them (where a table-of-contents link lands).
  std::vector<std::pair<std::string, size_t>> anchors;
};

XhtmlResult xhtmlToMarked(const std::string& xhtml);

}  // namespace pos::reader
