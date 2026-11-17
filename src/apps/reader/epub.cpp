#include "apps/reader/epub.h"

#include <algorithm>
#include <map>

#include "apps/reader/xml.h"
#include "core/strings.h"

namespace pos::reader {

namespace {

using xml::Event;
using xml::Tokenizer;

std::string dirOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? "" : path.substr(0, slash + 1);
}

std::string percentDecode(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) && std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}

std::string oneLine(const std::string& s) {
  // Titles and names: no white space at the ends, runs of it inside become one space.
  std::string out;
  for (char c : trimmed(s)) {
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (out.empty() || out.back() != ' ') out += ' ';
    } else {
      out += c;
    }
  }
  return out;
}

bool isDocument(const std::string& mediaType) { return mediaType == "application/xhtml+xml" || mediaType == "text/html" || mediaType == "application/x-dtbook+xml"; }

struct Item {
  std::string id, href, type, props;
};

bool hasWord(const std::string& list, const std::string& word) {
  size_t i = 0;
  while (i < list.size()) {
    size_t e = list.find(' ', i);
    if (e == std::string::npos) e = list.size();
    if (list.compare(i, e - i, word) == 0 && e - i == word.size()) return true;
    i = e + 1;
  }
  return false;
}

// href (with fragment stripped, resolved) -> first title, in document order.
struct TitleMap {
  std::map<std::string, std::string> first;  // document -> its first entry's title
  std::vector<TocEntry> entries;
  bool empty() const { return entries.empty(); }
};

void addTitle(TitleMap& m, const std::string& baseDir, const std::string& href, const std::string& title) {
  const std::string path = resolveHref(baseDir, href);
  const std::string t = oneLine(title);
  if (path.empty() || t.empty()) return;
  m.first.emplace(path, t);  // the first one wins
  const size_t hash = href.find('#');
  m.entries.push_back(TocEntry{path, hash == std::string::npos ? "" : percentDecode(href.substr(hash + 1)), t});
}

// EPUB 3 navigation document: <nav epub:type="toc"><ol><li><a href="ch1.xhtml">Title</a>...
void parseNav(const std::string& doc, const std::string& baseDir, TitleMap& out) {
  Tokenizer tk(doc);
  Event e;
  int navDepth = 0;
  bool seenToc = false, inA = false;
  std::string href, text;
  // Prefer the <nav> whose epub:type is "toc"; otherwise the first <nav>.
  bool anyToc = doc.find("epub:type=\"toc\"") != std::string::npos || doc.find("epub:type='toc'") != std::string::npos;
  bool inWanted = false;
  while (tk.next(e)) {
    if (e.type == Event::Start && e.name == "nav") {
      ++navDepth;
      const std::string t = e.attr("epub:type");
      const bool isToc = hasWord(t, "toc");
      if (!inWanted && (anyToc ? isToc : !seenToc)) inWanted = true;
      seenToc = true;
    } else if (e.type == Event::End && e.name == "nav") {
      if (--navDepth <= 0) {
        navDepth = 0;
        if (inWanted) return;
      }
    } else if (inWanted && e.type == Event::Start && e.name == "a") {
      inA = true;
      href = e.attr("href");
      text.clear();
    } else if (inWanted && e.type == Event::End && e.name == "a") {
      if (inA) addTitle(out, baseDir, href, text);
      inA = false;
    } else if (inA && e.type == Event::Text) {
      text += e.text;
    }
  }
}

// EPUB 2 NCX: <navPoint><navLabel><text>Title</text></navLabel><content src="ch1.xhtml"/></navPoint>
void parseNcx(const std::string& doc, const std::string& baseDir, TitleMap& out) {
  Tokenizer tk(doc);
  Event e;
  std::vector<std::string> labels;  // one per open navPoint
  std::vector<std::string> srcs;
  bool inLabelText = false;
  while (tk.next(e)) {
    if (e.type == Event::Start && e.name == "navpoint") {
      labels.emplace_back();
      srcs.emplace_back();
    } else if (e.type == Event::Start && e.name == "text" && !labels.empty()) {
      inLabelText = true;
    } else if (e.type == Event::End && e.name == "text") {
      inLabelText = false;
    } else if (e.type == Event::Start && e.name == "content" && !srcs.empty()) {
      if (srcs.back().empty()) srcs.back() = e.attr("src");
      // A navPoint's own label comes before its <content>, nested points after: the pair is complete here.
      if (!labels.back().empty()) addTitle(out, baseDir, srcs.back(), labels.back());
    } else if (e.type == Event::Text && inLabelText && !labels.empty() && srcs.back().empty()) {
      labels.back() += e.text;
    } else if (e.type == Event::End && e.name == "navpoint") {
      if (!labels.empty()) {
        labels.pop_back();
        srcs.pop_back();
      }
    }
  }
}

}  // namespace

std::string resolveHref(const std::string& baseDir, const std::string& hrefIn) {
  std::string href = hrefIn;
  const size_t cut = href.find_first_of("#?");
  if (cut != std::string::npos) href.erase(cut);
  href = percentDecode(href);
  if (href.empty()) return "";
  std::string joined = (href[0] == '/') ? href.substr(1) : baseDir + href;
  std::vector<std::string> parts;
  size_t i = 0;
  while (i <= joined.size()) {
    size_t e = joined.find('/', i);
    if (e == std::string::npos) e = joined.size();
    const std::string seg = joined.substr(i, e - i);
    if (seg == "..") {
      if (!parts.empty()) parts.pop_back();
    } else if (!seg.empty() && seg != ".") {
      parts.push_back(seg);
    }
    i = e + 1;
  }
  std::string out;
  for (size_t k = 0; k < parts.size(); ++k) out += (k ? "/" : "") + parts[k];
  return out;
}

bool parseEpub(zip::Archive& zip, EpubInfo& out, std::string& error) {
  out = EpubInfo{};
  std::string container;
  if (!zip.read("META-INF/container.xml", container, 256 * 1024)) {
    error = "Not an EPUB (no container.xml)";
    return false;
  }
  if (zip.find("META-INF/encryption.xml")) {
    // Fonts are often obfuscated through encryption.xml; real DRM names other algorithms. Only refuse the latter.
    std::string enc;
    zip.read("META-INF/encryption.xml", enc, 256 * 1024);
    const bool onlyFontMangling = enc.find("EncryptedData") == std::string::npos ||
                                  (enc.find("idpf.org/2008/embedding") != std::string::npos || enc.find("ns.adobe.com/pdf/enc") != std::string::npos);
    if (!onlyFontMangling) {
      error = "This book is protected (DRM)";
      return false;
    }
  }
  {
    Tokenizer tk(container);
    Event e;
    while (tk.next(e))
      if (e.type == Event::Start && e.name == "rootfile" && out.opfPath.empty()) out.opfPath = resolveHref("", e.attr("full-path"));
  }
  if (out.opfPath.empty()) {
    error = "Not an EPUB (no package file)";
    return false;
  }
  std::string opf;
  if (!zip.read(out.opfPath, opf, 4 * 1024 * 1024)) {
    error = "Not an EPUB (package file unreadable: " + zip.error() + ")";
    return false;
  }
  const std::string base = dirOf(out.opfPath);

  std::map<std::string, Item> manifest;
  std::vector<std::string> spineIds;
  std::vector<std::string> creators;
  std::string coverId, tocId, title;
  {
    Tokenizer tk(opf);
    Event e;
    std::string field;  // "title" / "creator" while inside such an element
    std::string text;
    bool creatorIsAuthor = true;
    while (tk.next(e)) {
      if (e.type == Event::Start) {
        const std::string n = xml::localName(e.name);
        if (n == "title" && e.name.find(':') != std::string::npos && title.empty()) {
          field = "title";
          text.clear();
        } else if (n == "creator" && e.name.find(':') != std::string::npos) {
          field = "creator";
          text.clear();
          const std::string role = e.attr("opf:role");
          creatorIsAuthor = role.empty() || role == "aut";
        } else if (n == "meta") {
          if (e.attr("name") == "cover") coverId = e.attr("content");
        } else if (n == "item") {
          Item it;
          it.id = e.attr("id");
          it.href = e.attr("href");
          it.type = e.attr("media-type");
          it.props = e.attr("properties");
          if (!it.id.empty()) manifest[it.id] = it;
        } else if (n == "spine") {
          tocId = e.attr("toc");
        } else if (n == "itemref") {
          if (e.attr("linear") != "no") spineIds.push_back(e.attr("idref"));
        }
      } else if (e.type == Event::Text && !field.empty()) {
        text += e.text;
      } else if (e.type == Event::End && !field.empty() && xml::localName(e.name) == field) {
        if (field == "title" && title.empty()) title = oneLine(text);
        else if (field == "creator" && creatorIsAuthor) {
          const std::string c = oneLine(text);
          if (!c.empty()) creators.push_back(c);
        }
        field.clear();
      }
    }
  }
  out.title = title;
  for (size_t i = 0; i < creators.size() && i < 3; ++i) out.author += (i ? ", " : "") + creators[i];

  // Cover: EPUB 3 "cover-image" property, else the EPUB 2 <meta name="cover" content="id">.
  for (const auto& kv : manifest)
    if (hasWord(kv.second.props, "cover-image")) {
      out.cover = resolveHref(base, kv.second.href);
      out.coverType = kv.second.type;
      break;
    }
  if (out.cover.empty() && !coverId.empty()) {
    auto it = manifest.find(coverId);
    if (it != manifest.end() && it->second.type.rfind("image/", 0) == 0) {
      out.cover = resolveHref(base, it->second.href);
      out.coverType = it->second.type;
    }
  }
  if (!out.cover.empty() && !zip.find(out.cover)) out.cover.clear();

  // Table of contents: EPUB 3 nav document, else the NCX.
  TitleMap titles;
  for (const auto& kv : manifest)
    if (hasWord(kv.second.props, "nav")) {
      std::string doc;
      if (zip.read(resolveHref(base, kv.second.href), doc, 2 * 1024 * 1024)) parseNav(doc, dirOf(resolveHref(base, kv.second.href)), titles);
      break;
    }
  if (titles.empty()) {
    std::string ncxHref;
    auto it = manifest.find(tocId);
    if (it != manifest.end()) ncxHref = it->second.href;
    if (ncxHref.empty())
      for (const auto& kv : manifest)
        if (kv.second.type == "application/x-dtbncx+xml") {
          ncxHref = kv.second.href;
          break;
        }
    if (!ncxHref.empty()) {
      std::string doc;
      const std::string path = resolveHref(base, ncxHref);
      if (zip.read(path, doc, 2 * 1024 * 1024)) parseNcx(doc, dirOf(path), titles);
    }
  }

  out.toc = titles.entries;
  for (const std::string& id : spineIds) {
    auto it = manifest.find(id);
    if (it == manifest.end() || !isDocument(it->second.type)) continue;
    EpubChapter c;
    c.href = resolveHref(base, it->second.href);
    if (c.href.empty() || !zip.find(c.href)) continue;  // listed but not in the zip: skip
    auto t = titles.first.find(c.href);
    if (t != titles.first.end()) c.title = t->second;
    out.chapters.push_back(std::move(c));
  }
  if (out.chapters.empty()) {
    error = "This EPUB has no readable chapters";
    return false;
  }
  return true;
}

}  // namespace pos::reader
