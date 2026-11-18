#include "apps/hackernews/hackernews_model.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdlib>

#include "core/strings.h"
#include "core/text_clean.h"

namespace pos::hackernews {

namespace {

constexpr const char* kOrderBase = "https://hacker-news.firebaseio.com/v0/";
constexpr const char* kSearchBase = "https://hn.algolia.com/api/v1/search?tags=";
constexpr const char* kDot = " \xC2\xB7 ";  // " · "
constexpr const char* kNbsp = "\xC2\xA0";
constexpr char kSoftBreak = 0x06;  // ui::parseMarked: the line continues the block before it

struct SectionInfo {
  const char *name, *key, *list;
};
constexpr SectionInfo kSectionInfo[kSections] = {
    {"Top", "top", "topstories"}, {"New", "new", "newstories"}, {"Ask", "ask", "askstories"}, {"Show", "show", "showstories"}};

const SectionInfo& info(Section s) {
  const int i = static_cast<int>(s);
  return kSectionInfo[i >= 0 && i < kSections ? i : 0];
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

// A JSON string member as text the fonts can draw, "" if it is missing or null.
std::string textOf(JsonVariantConst v) {
  const char* s = v.as<const char*>();
  return s ? cleanText(s) : std::string();
}

// What to tell the user about a request that did not give a usable answer.
std::string errorText(const hal::HttpResponse& r) {
  if (r.status == 0) return r.error.empty() ? std::string("No connection") : r.error;
  if (r.status != 200) return "Hacker News answered " + std::to_string(r.status);
  return r.error.empty() ? std::string("Unexpected answer") : r.error;  // 200 with an error: the answer was cut (too large)
}

hal::HttpResponse get(const Transport& send, const std::string& url) {
  hal::HttpRequest req;
  req.url = url;
  req.timeoutMs = 15000;
  return send(req);
}

// One block of a comment (what stands between two <p>): text, or text around a code block.
void appendBlock(const std::string& block, std::string& out) {
  auto addLine = [&out](const std::string& line, bool continues) {
    if (line.empty()) return;
    if (!out.empty()) out += '\n';
    if (continues) out += kSoftBreak;
    out += line;
  };
  CleanOptions opt;
  opt.html = true;
  opt.shortenLinks = false;
  auto addText = [&](const std::string& html) {
    std::string line = cleanText(html, opt);
    std::replace(line.begin(), line.end(), '\n', ' ');
    addLine(line, false);
  };
  size_t at = 0;
  while (at < block.size()) {
    const size_t pre = block.find("<pre>", at);
    if (pre == std::string::npos) {
      addText(block.substr(at));
      break;
    }
    addText(block.substr(at, pre - at));
    size_t end = block.find("</pre>", pre);
    if (end == std::string::npos) end = block.size();
    // Code: its lines stay lines, and what a line is moved in by stays (as no-break spaces, which the layout keeps).
    bool first = true;
    for (const std::string& raw : split(block.substr(pre + 5, end - pre - 5), '\n')) {
      size_t lead = 0, cols = 0;
      while (lead < raw.size() && (raw[lead] == ' ' || raw[lead] == '\t')) cols += raw[lead++] == '\t' ? 2 : 1;
      const std::string code = cleanText(raw, opt);
      if (code.empty()) continue;
      std::string line;
      for (size_t i = 0; i < std::min<size_t>(cols, 12); ++i) line += kNbsp;
      addLine(line + code, !first);
      first = false;
    }
    at = std::min(block.size(), end + 6);
  }
}

}  // namespace

const char* sectionName(Section s) { return info(s).name; }
const char* sectionKey(Section s) { return info(s).key; }

bool sectionFromKey(const std::string& key, Section& out) {
  for (int i = 0; i < kSections; ++i) {
    if (key != kSectionInfo[i].key) continue;
    out = static_cast<Section>(i);
    return true;
  }
  return false;
}

// ---- addresses

std::string idsUrl(Section s) { return std::string(kOrderBase) + info(s).list + ".json"; }
std::string itemUrl(uint32_t id) { return std::string(kOrderBase) + "item/" + std::to_string(id) + ".json"; }
std::string siteUrl(uint32_t id) { return "https://news.ycombinator.com/item?id=" + std::to_string(id); }

std::string storiesUrl(const std::vector<uint32_t>& ids) {
  std::string url = std::string(kSearchBase) + "(story,poll),(";
  for (size_t i = 0; i < ids.size(); ++i) url += (i ? ",story_" : "story_") + std::to_string(ids[i]);
  return url + ")&hitsPerPage=" + std::to_string(ids.size()) +
         "&attributesToRetrieve=title,url,author,points,num_comments,created_at_i&attributesToHighlight=none";
}

std::string commentsUrl(uint32_t storyId) {
  return std::string(kSearchBase) + "comment,story_" + std::to_string(storyId) + "&hitsPerPage=" + std::to_string(kCommentsMax) +
         "&attributesToRetrieve=author,comment_text,parent_id,created_at_i&attributesToHighlight=none";
}

// ---- answers

bool parseIds(const std::string& json, std::vector<uint32_t>& out, int max) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonArrayConst>()) return false;
  out.clear();
  for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
    if (static_cast<int>(out.size()) >= max) break;
    const uint32_t id = v.as<uint32_t>();
    if (id) out.push_back(id);
  }
  return true;
}

bool parseStories(const std::string& json, const std::vector<uint32_t>& ids, std::vector<Story>& out, int max) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc["hits"].is<JsonArrayConst>()) return false;
  std::vector<Story> found;
  for (JsonObjectConst hit : doc["hits"].as<JsonArrayConst>()) {
    Story s;
    const char* id = hit["objectID"].as<const char*>();
    s.id = id ? static_cast<uint32_t>(std::strtoul(id, nullptr, 10)) : 0;
    s.title = textOf(hit["title"]);
    if (!s.id || s.title.empty()) continue;
    const char* url = hit["url"].as<const char*>();
    s.url = url ? url : "";
    s.by = textOf(hit["author"]);
    s.points = hit["points"] | 0;
    s.comments = hit["num_comments"] | 0;
    s.time = hit["created_at_i"] | static_cast<int64_t>(0);
    found.push_back(std::move(s));
  }
  out.clear();
  for (uint32_t id : ids) {
    if (static_cast<int>(out.size()) >= max) break;
    for (Story& s : found) {
      if (s.id != id) continue;
      out.push_back(std::move(s));
      s.id = 0;  // an id named twice shows once
      break;
    }
  }
  return true;
}

bool parseItem(const std::string& json, Story& out, std::vector<uint32_t>& kids) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObjectConst>()) return false;  // "null": no such item
  const uint32_t id = doc["id"] | static_cast<uint32_t>(0);
  if (!id) return false;
  out.id = id;
  if (doc["title"].is<const char*>()) out.title = textOf(doc["title"]);
  if (doc["url"].is<const char*>()) out.url = doc["url"].as<const char*>();
  if (doc["by"].is<const char*>()) out.by = textOf(doc["by"]);
  if (doc["score"].is<int>()) out.points = doc["score"].as<int>();
  if (doc["descendants"].is<int>()) out.comments = doc["descendants"].as<int>();
  if (doc["time"].is<int64_t>()) out.time = doc["time"].as<int64_t>();
  const char* text = doc["text"].as<const char*>();
  out.text = text ? markedText(text) : std::string();
  kids.clear();
  for (JsonVariantConst v : doc["kids"].as<JsonArrayConst>())
    if (const uint32_t kid = v.as<uint32_t>()) kids.push_back(kid);
  return true;
}

bool parseComments(const std::string& json, uint32_t storyId, const std::vector<uint32_t>& kidOrder, Thread& out) {
  out = Thread{};
  std::vector<Comment> all;
  {
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc["hits"].is<JsonArrayConst>()) return false;
    const JsonArrayConst hits = doc["hits"].as<JsonArrayConst>();
    all.reserve(hits.size());
    for (JsonObjectConst hit : hits) {
      Comment c;
      const char* id = hit["objectID"].as<const char*>();
      c.id = id ? static_cast<uint32_t>(std::strtoul(id, nullptr, 10)) : 0;
      c.parent = hit["parent_id"] | static_cast<uint32_t>(0);
      if (!c.id || !c.parent) continue;
      c.by = textOf(hit["author"]);
      c.time = hit["created_at_i"] | static_cast<int64_t>(0);
      const char* text = hit["comment_text"].as<const char*>();
      c.text = text ? markedText(text) : std::string();
      all.push_back(std::move(c));
    }
    out.total = std::max<int>(doc["nbHits"] | 0, static_cast<int>(all.size()));
  }  // the document (as large as the answer) is gone before the tree is made

  const int n = static_cast<int>(all.size());
  // Each comment's parent as an index: -1 the story, -2 not among the hits.
  std::vector<std::pair<uint32_t, int>> byId;
  byId.reserve(all.size());
  for (int i = 0; i < n; ++i) byId.emplace_back(all[i].id, i);
  std::sort(byId.begin(), byId.end());
  auto indexOf = [&byId](uint32_t id) {
    auto it = std::lower_bound(byId.begin(), byId.end(), std::make_pair(id, -1));
    return it != byId.end() && it->first == id ? it->second : -2;
  };
  std::vector<int> parent(all.size());
  for (int i = 0; i < n; ++i) parent[i] = all[i].parent == storyId ? -1 : indexOf(all[i].parent);
  // The place of an answer to the story: as the site orders them; one the site's list does not name comes after those.
  std::vector<std::pair<uint32_t, int>> placeOf;
  placeOf.reserve(kidOrder.size());
  for (size_t k = 0; k < kidOrder.size(); ++k) placeOf.emplace_back(kidOrder[k], static_cast<int>(k));
  std::sort(placeOf.begin(), placeOf.end());
  auto rank = [&](int i) -> uint64_t {
    if (parent[i] != -1) return all[i].id;  // replies: oldest first
    auto it = std::lower_bound(placeOf.begin(), placeOf.end(), std::make_pair(all[i].id, -1));
    if (it != placeOf.end() && it->first == all[i].id) return static_cast<uint64_t>(it->second);
    return (static_cast<uint64_t>(1) << 32) + all[i].id;
  };
  // Children of one parent next to each other, in their order: slot 0 the story's, slot i + 1 comment i's.
  std::vector<int> ord;
  ord.reserve(all.size());
  for (int i = 0; i < n; ++i)
    if (parent[i] != -2) ord.push_back(i);
  std::vector<uint64_t> ranks(all.size());
  for (int i : ord) ranks[i] = rank(i);
  std::sort(ord.begin(), ord.end(), [&](int a, int b) { return parent[a] != parent[b] ? parent[a] < parent[b] : ranks[a] < ranks[b]; });
  std::vector<int> begin(all.size() + 2, 0);
  for (int i : ord) ++begin[parent[i] + 2];
  for (size_t s = 1; s < begin.size(); ++s) begin[s] += begin[s - 1];  // children of slot s: ord[begin[s] .. begin[s + 1])

  // Depth first without recursion (a thread can be fifty replies deep; the worker's stack is 16 KB).
  struct Level {
    int at, end;
  };
  std::vector<Level> stack{{begin[0], begin[1]}};
  out.comments.reserve(ord.size());
  while (!stack.empty()) {
    Level& top = stack.back();
    if (top.at >= top.end) {
      stack.pop_back();
      continue;
    }
    const int i = ord[top.at++];
    const int depth = static_cast<int>(stack.size()) - 1;
    const bool hasReplies = begin[i + 1] < begin[i + 2];
    if (all[i].text.empty()) {
      if (!hasReplies) continue;  // a deleted comment nobody answered
      all[i].text = "[deleted]";
    }
    all[i].depth = depth;
    out.comments.push_back(std::move(all[i]));
    if (hasReplies) stack.push_back({begin[i + 1], begin[i + 2]});  // (`top` is not used after this)
  }
  // Replies below each comment: everything after it that is deeper, up to the next one that is not.
  std::vector<int> open;
  for (int i = 0; i < static_cast<int>(out.comments.size()); ++i) {
    while (!open.empty() && out.comments[open.back()].depth >= out.comments[i].depth) open.pop_back();
    for (int o : open) ++out.comments[o].replies;
    open.push_back(i);
  }
  return true;
}

std::string markedText(const std::string& html) {
  std::string out;
  size_t at = 0;
  while (at <= html.size()) {
    size_t end = html.find("<p>", at);
    if (end == std::string::npos) end = html.size();
    appendBlock(html.substr(at, end - at), out);
    at = end + 3;
  }
  return out;
}

// ---- reads

ListResult fetchList(const Transport& send, Section s) {
  ListResult res;
  hal::HttpResponse r = get(send, idsUrl(s));
  std::vector<uint32_t> ids;
  if (r.status != 200 || !r.error.empty() || r.body.size() > kMaxListBytes || !parseIds(r.body, ids)) {
    res.error = errorText(r);
    return res;
  }
  if (ids.empty()) {
    res.ok = true;
    return res;
  }
  r = get(send, storiesUrl(ids));
  if (r.status != 200 || !r.error.empty() || r.body.size() > kMaxListBytes || !parseStories(r.body, ids, res.stories)) {
    res.error = errorText(r);
    return res;
  }
  res.ok = true;
  return res;
}

ThreadResult fetchThread(const Transport& send, const Story& known) {
  ThreadResult res;
  res.story = known;
  hal::HttpResponse r = get(send, itemUrl(known.id));
  if (r.status != 200 || !r.error.empty() || r.body.size() > kMaxListBytes) {
    res.error = errorText(r);
    return res;
  }
  std::vector<uint32_t> kids;
  parseItem(r.body, res.story, kids);  // "null" (an item the first API does not have): what the list knew stays
  if (kids.empty() && res.story.comments <= 0) {
    res.ok = true;  // nothing to read
    return res;
  }
  r = get(send, commentsUrl(known.id));
  if (r.status != 200 || !r.error.empty() || r.body.size() > kMaxThreadBytes ||
      !parseComments(r.body, known.id, kids, res.thread)) {
    res.error = errorText(r);
    return res;
  }
  res.ok = true;
  return res;
}

// ---- text

std::string ageText(int64_t time, int64_t now) {
  if (time <= 0 || now <= 0) return "";
  const int64_t d = now - time;
  if (d < 60) return "now";
  if (d < 3600) return std::to_string(d / 60) + " min";
  if (d < 48 * 3600) return std::to_string(d / 3600) + " h";
  return std::to_string(d / 86400) + " d";
}

namespace {
void addPart(std::string& line, const std::string& part) {
  if (part.empty()) return;
  if (!line.empty()) line += kDot;
  line += part;
}
}  // namespace

std::string metaLine(const Story& s, int64_t now) {
  std::string line = plural(s.points, "point", "points");
  addPart(line, plural(s.comments, "comment", "comments"));
  addPart(line, ageText(s.time, now));
  addPart(line, hostOfUrl(s.url));
  return line;
}

std::string storyLine(const Story& s, int64_t now) {
  std::string line = plural(s.points, "point", "points");
  if (!s.by.empty()) addPart(line, "by " + s.by);
  addPart(line, ageText(s.time, now));
  addPart(line, hostOfUrl(s.url));
  return line;
}

ThreadText threadText(const Story& story, const Thread& thread, int64_t now) {
  ThreadText t;
  if (!story.text.empty()) t.paras = ui::parseMarked(story.text);
  t.paras.reserve(t.paras.size() + thread.comments.size() * 3);
  for (const Comment& c : thread.comments) {
    const int margin = std::min(c.depth, kDepthMax) * kDepthStep;
    if (c.depth == 0) t.topLevel.push_back(static_cast<int>(t.paras.size()));
    t.ids.push_back(c.id);
    t.firstPara.push_back(static_cast<int>(t.paras.size()));
    ui::Paragraph byline;
    byline.heading = 4;
    byline.margin = margin;
    std::string who = c.by.empty() ? std::string("[deleted]") : c.by;
    addPart(who, ageText(c.time, now));
    if (c.replies > 0) addPart(who, plural(c.replies, "reply", "replies"));
    byline.runs.push_back(ui::Run{std::move(who), false, false});
    t.paras.push_back(std::move(byline));
    for (ui::Paragraph& p : ui::parseMarked(c.text)) {
      p.margin = margin;
      t.paras.push_back(std::move(p));
    }
  }
  if (thread.total > kCommentsMax) {
    ui::Paragraph note;
    note.heading = 4;
    note.runs.push_back(ui::Run{"The first " + std::to_string(kCommentsMax) + " of " + std::to_string(thread.total) + " comments are shown.", false, false});
    t.paras.push_back(std::move(note));
  }
  return t;
}

int nextTopLevel(const ThreadText& t, int after) {
  auto it = std::upper_bound(t.topLevel.begin(), t.topLevel.end(), after);
  return it == t.topLevel.end() ? -1 : *it;
}

Anchor anchorOf(const ThreadText& t, ui::TextPos pos) {
  Anchor a;
  a.para = pos.para;
  a.byte = pos.byte;
  auto it = std::upper_bound(t.firstPara.begin(), t.firstPara.end(), pos.para);
  if (it == t.firstPara.begin()) return a;  // in the story's own text
  const size_t i = static_cast<size_t>(it - t.firstPara.begin()) - 1;
  a.comment = t.ids[i];
  a.para = pos.para - t.firstPara[i];
  return a;
}

ui::TextPos posOf(const ThreadText& t, const Anchor& a) {
  int first = 0, end = t.firstPara.empty() ? static_cast<int>(t.paras.size()) : t.firstPara[0];  // the story's own text
  if (a.comment != 0) {
    const auto it = std::find(t.ids.begin(), t.ids.end(), a.comment);
    if (it == t.ids.end()) return ui::TextPos{};
    const size_t i = static_cast<size_t>(it - t.ids.begin());
    first = t.firstPara[i];
    end = i + 1 < t.firstPara.size() ? t.firstPara[i + 1] : static_cast<int>(t.paras.size());
  }
  if (a.para < 0 || first + a.para >= end) return ui::TextPos{first, 0};  // the comment got shorter: its start
  return ui::TextPos{first + a.para, std::max(0, a.byte)};
}

// ---- the saved copy

std::string packList(int64_t fetched, const std::vector<Story>& stories) {
  JsonDocument doc;
  doc["t"] = fetched;
  JsonArray list = doc["s"].to<JsonArray>();
  for (const Story& s : stories) {
    JsonArray a = list.add<JsonArray>();
    a.add(s.id);
    a.add(s.title);
    a.add(s.url);
    a.add(s.by);
    a.add(s.points);
    a.add(s.comments);
    a.add(s.time);
  }
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool unpackList(const std::string& saved, int64_t& fetched, std::vector<Story>& stories) {
  JsonDocument doc;
  if (saved.empty() || deserializeJson(doc, saved) || !doc["s"].is<JsonArrayConst>()) return false;
  fetched = doc["t"] | static_cast<int64_t>(0);
  stories.clear();
  for (JsonArrayConst a : doc["s"].as<JsonArrayConst>()) {
    Story s;
    s.id = a[0] | static_cast<uint32_t>(0);
    s.title = a[1] | "";
    s.url = a[2] | "";
    s.by = a[3] | "";
    s.points = a[4] | 0;
    s.comments = a[5] | 0;
    s.time = a[6] | static_cast<int64_t>(0);
    if (s.id && !s.title.empty()) stories.push_back(std::move(s));
  }
  return true;
}

}  // namespace pos::hackernews
