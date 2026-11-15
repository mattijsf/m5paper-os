// EPUB structure (docs/apps/reader.md "Pipeline" steps 1-2): container.xml -> OPF (title, author, cover, spine, manifest)
// -> table of contents (EPUB 3 nav document or EPUB 2 NCX). The chapter texts are read later (xhtml.h).
#pragma once

#include <string>
#include <vector>

#include "core/zip.h"

namespace pos::reader {

struct EpubChapter {
  std::string href;   // path of the XHTML document inside the zip
  std::string title;  // from the table of contents, "" if it has none for this document
};

// One link of the table of contents: a document of the zip, optionally an anchor (#id) inside it.
struct TocEntry {
  std::string path, fragment, title;
};

struct EpubInfo {
  std::string title, author;
  std::string opfPath;
  std::string cover;      // zip path of the cover image, "" if none
  std::string coverType;  // media type of the cover ("image/jpeg", "image/png"), "" if unknown
  std::vector<TocEntry> toc;          // the table of contents in reading order (anchors included)
  std::vector<EpubChapter> chapters;  // the spine, in reading order (documents only)
};

// Reads the structure. False with `error` set if this is not a usable EPUB (no container, no OPF, empty spine,
// encrypted). Tolerant of the usual sloppiness: missing metadata, missing TOC, odd namespaces, %20 in hrefs.
bool parseEpub(zip::Archive& zip, EpubInfo& out, std::string& error);

// Resolves an href found in a document at `baseDir` ("OEBPS/" or "") to a zip path: drops #fragment and ?query,
// decodes %XX, handles "./", "../" and a leading "/".
std::string resolveHref(const std::string& baseDir, const std::string& href);

}  // namespace pos::reader
