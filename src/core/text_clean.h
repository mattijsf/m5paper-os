// Text clean-up for messages and mail (docs/apps/inbox.md "Behaviour"): HTML to text, entities, emoji, links, blank lines.
// Pure functions, shared by the inbox and the reader's XML tokenizer (entities).
#pragma once

#include <string>

namespace pos {

// &amp; &#8217; &#x2019; &eacute; ... Unknown entities stay as written.
std::string decodeEntities(const std::string& s);
// Appends a code point as UTF-8 (0, surrogates and out-of-range become U+FFFD).
void appendUtf8(std::string& out, unsigned cp);

// "https://www.example.com/a/b?x=1" -> "example.com" (scheme, "www.", port, path, query and fragment dropped); "" if `url` is not a URL.
std::string hostOfUrl(const std::string& url);

struct CleanOptions {
  bool html = false;       // the text is HTML (a mail body): tags are removed, entities decoded, blocks become lines
  bool shortenLinks = true;  // http(s) links become their host name ("example.com")
  size_t maxWord = 60;     // a word longer than this is cut with an ellipsis (nothing wider than the column is wanted)
};

// What can be read on the panel: invalid UTF-8 and control characters dropped, characters without a glyph (emoji and the like)
// replaced by a short text form or dropped, CR LF to LF, tabs and runs of blanks to one space, at most one blank line in a row,
// no blank lines or spaces at the ends. Never longer than the input plus a few bytes per emoji.
std::string cleanText(const std::string& in, const CleanOptions& opt = {});

// Whether the baked fonts have a glyph for the code point (the ranges of tools/gen-assets.sh; keep in sync with ui/text.cpp).
bool hasGlyph(unsigned cp);

}  // namespace pos
