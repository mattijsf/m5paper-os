#include "ui/label.h"

#include <algorithm>

#include "core/utf8.h"

namespace pos::ui {

namespace {

// Word-wraps one paragraph (already sanitized) into `out`.
void wrapInto(Canvas& c, const std::string& s, Type type, int width, std::vector<std::string>& out) {
  std::string line;
  size_t i = 0;
  auto fits = [&](const std::string& t) { return measureText(c, t, type) <= width; };
  while (i < s.size()) {
    if (s[i] == ' ') {  // spaces only separate words
      ++i;
      continue;
    }
    // next word
    size_t j = i;
    while (j < s.size() && s[j] != ' ') j += utf8::span(s, j);
    std::string word = s.substr(i, j - i);
    std::string trial = line.empty() ? word : line + " " + word;
    if (fits(trial)) {
      line = trial;
      i = j;
      continue;
    }
    if (!line.empty()) {  // break before the word
      out.push_back(line);
      line.clear();
      continue;
    }
    // A single word wider than the line: break it by code points.
    std::string part;
    size_t k = 0;
    while (k < word.size()) {
      size_t n = utf8::span(word, k);
      if (!part.empty() && !fits(part + word.substr(k, n))) break;
      part += word.substr(k, n);
      k += n;
    }
    out.push_back(part);
    i += k;
  }
  if (!line.empty()) out.push_back(line);
}

}  // namespace

void Label::setText(const std::string& t, Hint h) {
  if (t == text_) return;
  text_ = t;
  invalidate(h);
}

std::vector<std::string> Label::lines(Canvas& c, int width) const {
  std::vector<std::string> out;
  if (keepLineBreaks) {
    size_t i = 0;
    while (i <= text_.size()) {
      size_t j = text_.find('\n', i);
      if (j == std::string::npos) j = text_.size();
      const size_t before = out.size();
      wrapInto(c, sanitize(text_.substr(i, j - i)), type, width, out);
      if (out.size() == before) out.push_back("");  // an empty line stays
      i = j + 1;
    }
  } else {
    wrapInto(c, sanitize(text_), type, width, out);
  }
  if (out.empty()) out.push_back("");

  if (maxLines > 0 && static_cast<int>(out.size()) > maxLines) {
    // Rejoin the cut text into the last allowed line and ellipsize it.
    std::string last;
    for (size_t k = maxLines - 1; k < out.size(); ++k) last += (last.empty() ? "" : " ") + out[k];
    out.resize(maxLines);
    out.back() = ellipsize(c, last, type, width);
  }
  return out;
}

Size Label::measure(int availW) {
  Canvas* c = canvas();
  if (!c) return {0, 0};
  auto ls = lines(*c, availW);
  int w = 0;
  for (auto& l : ls) w = std::max(w, measureText(*c, l, type));
  const int lh = lineHeight(*c, type);
  return {w, static_cast<int>(ls.size()) * lh + (static_cast<int>(ls.size()) - 1) * lineGap};
}

void Label::draw(Canvas& c) {
  auto ls = lines(c, frame.w);
  const int lh = lineHeight(c, type);
  int y = frame.y;
  for (auto& l : ls) {
    int x = frame.x;
    Anchor a = Anchor::TopLeft;
    if (align == TextAlign::Center) {
      x = frame.x + frame.w / 2;
      a = Anchor::TopCenter;
    } else if (align == TextAlign::Right) {
      x = frame.x + frame.w;
      a = Anchor::TopRight;
    }
    drawText(c, l, x, y, type, color, a, bg());
    y += lh + lineGap;
  }
}

void IconView::draw(Canvas& c) {
  drawIcon(c, icon, frame.x + frame.w / 2, frame.y + frame.h / 2, size, color, bg());
}

}  // namespace pos::ui
