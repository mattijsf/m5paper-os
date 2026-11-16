#include "ui/keyboard.h"

#include <algorithm>

#include "core/clipboard.h"
#include "ui/controls.h"
#include "ui/label.h"
#include "ui/panels.h"
#include "ui/root.h"

namespace pos::ui {

namespace {

void addRow(std::vector<std::string>& out, const char* chars) {
  for (const char* p = chars; *p; ++p) out.push_back(std::string(1, *p));
}

}  // namespace

Keyboard::Keyboard() {
  for (int l = 0; l < 3; ++l) buildLayer(l, layers_[l]);
}

void Keyboard::buildLayer(int layer, std::vector<Key>& keys) const {
  auto chars = [&](int row, int x0, const std::vector<std::string>& labels) {
    int x = x0;
    for (const auto& l : labels) {
      keys.push_back(Key{x, kKeyW, row, l, Action::Char, Icon::Check, false});
      x += kKeyW;
    }
  };
  std::vector<std::string> r0, r1, r2;
  if (layer == 2) {
    addRow(r0, "1234567890");
    addRow(r1, "-/:;()$&@\"");
    addRow(r2, ".,?!'+=");
  } else {
    const bool up = layer == 1;
    addRow(r0, up ? "QWERTYUIOP" : "qwertyuiop");
    addRow(r1, up ? "ASDFGHJKL" : "asdfghjkl");
    addRow(r2, up ? "ZXCVBNM" : "zxcvbnm");
  }
  chars(0, 0, r0);
  chars(1, layer == 2 ? 0 : kKeyW / 2, r1);
  // Row 2: shift (or a blank) + keys + backspace.
  if (layer == 2) keys.push_back(Key{0, 81, 2, "", Action::None, Icon::Check, false});
  else keys.push_back(Key{0, 81, 2, "", Action::Shift, Icon::ArrowUp, true});
  chars(2, 81, r2);
  keys.push_back(Key{kKeyW * 10 - 81, 81, 2, "", Action::Backspace, Icon::Delete, true});
  // Row 3: layer switch, comma, space, period, enter.
  if (layer == 2) keys.push_back(Key{0, 81, 3, "abc", Action::Letters, Icon::Check, false});
  else keys.push_back(Key{0, 81, 3, "123", Action::Symbols, Icon::Check, false});
  keys.push_back(Key{81, kKeyW, 3, ",", Action::Char, Icon::Check, false});
  keys.push_back(Key{81 + kKeyW, 216, 3, " ", Action::Space, Icon::Check, false});
  keys.push_back(Key{81 + kKeyW + 216, kKeyW, 3, ".", Action::Char, Icon::Check, false});
  keys.push_back(Key{81 + 2 * kKeyW + 216, kKeyW * 10 - (81 + 2 * kKeyW + 216), 3, "", Action::Enter, Icon::CornerDownLeft, true});
}

int Keyboard::keyAt(int px, int py) const {
  const int row = (py - frame.y) / kKeyH;
  const int x = px - frame.x;
  const auto& ks = keys();
  for (size_t i = 0; i < ks.size(); ++i)
    if (ks[i].row == row && x >= ks[i].x && x < ks[i].x + ks[i].w) return static_cast<int>(i);
  return -1;
}

void Keyboard::drawKey(Canvas& c, const Key& k, bool pressed) {
  if (k.action == Action::None) return;
  const Rect r = keyRect(k);
  const int inset = 3;
  const bool special = k.action != Action::Char && k.action != Action::Space;
  const Color back = pressed ? ink : (special ? paper : fill);
  const Color fg = pressed ? paper : ink;
  c.fillRoundRect(r.x + inset, r.y + inset, r.w - 2 * inset, r.h - 2 * inset, kRadiusSmall, back);
  if (special && !pressed) strokeRound(c, Rect{static_cast<int16_t>(r.x + inset), static_cast<int16_t>(r.y + inset), static_cast<int16_t>(r.w - 2 * inset), static_cast<int16_t>(r.h - 2 * inset)}, kRadiusSmall, 2, ink);
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  if (k.hasIcon) {
    drawIcon(c, k.icon, cx, cy, 32, fg, back);
  } else if (k.action != Action::Space) {
    drawText(c, k.label, cx, cy, k.label.size() > 1 ? Type::Body : Type::Title, fg, Anchor::MiddleCenter, back);
  }
}

void Keyboard::draw(Canvas& c) {
  for (const Key& k : keys()) drawKey(c, k, false);
  if (pressed_ >= 0 && pressed_ < static_cast<int>(keys().size())) drawKey(c, keys()[pressed_], true);
}

void Keyboard::press(int index) {
  pressed_ = index;
  invalidateRect(keyRect(keys()[index]), Hint::Feedback);
}

void Keyboard::release() {
  if (pressed_ < 0) return;
  const Rect r = keyRect(keys()[pressed_]);
  pressed_ = -1;
  invalidateRect(r, Hint::Feedback);
}

void Keyboard::commit(const Key& k) {
  switch (k.action) {
    case Action::Char:
    case Action::Space:
      if (onKey) onKey(Kind::Char, k.label);
      if (layer_ == 1) {  // ABC is one letter
        layer_ = 0;
        invalidate(Hint::Feedback);
      }
      break;
    case Action::Backspace:
      if (onKey) onKey(Kind::Backspace, "");
      break;
    case Action::Enter:
      if (onKey) onKey(Kind::Enter, "\n");
      break;
    case Action::Shift:
      layer_ = layer_ == 1 ? 0 : 1;
      invalidate(Hint::Feedback);
      break;
    case Action::Symbols:
      layer_ = 2;
      invalidate(Hint::Feedback);
      break;
    case Action::Letters:
      layer_ = 0;
      invalidate(Hint::Feedback);
      break;
    case Action::None:
      break;
  }
}

bool Keyboard::onTouch(const Gesture& g) {
  switch (g.type) {
    case Gesture::Down: {
      const int k = keyAt(g.x, g.y);
      if (k < 0 || keys()[k].action == Action::None) return true;
      Timers* timers = host() ? host()->timers() : nullptr;
      if (!timers) {
        press(k);
      } else {
        const uint32_t seq = ++pressSeq_;
        timers->after(kPressDelayMs, [this, k, seq, alive = std::weak_ptr<bool>(alive_)] {
          auto a = alive.lock();
          if (a && *a && seq == pressSeq_ && pressed_ < 0 && k < static_cast<int>(keys().size())) press(k);
        });
      }
      return true;
    }
    case Gesture::Tap: {
      ++pressSeq_;
      release();
      const int k = keyAt(g.startX, g.startY);
      if (k >= 0) {
        const Key key = keys()[k];  // copy: commit may switch layers
        commit(key);
      }
      return true;
    }
    case Gesture::LongPress:
    case Gesture::Cancel:
      ++pressSeq_;
      release();
      return true;
    default:
      return false;
  }
}

std::pair<int, int> Keyboard::keyCenter(int layer, size_t index) const {
  const Key& k = layers_[layer][index];
  return {frame.x + k.x + k.w / 2, frame.y + k.row * kKeyH + kKeyH / 2};
}

std::vector<KeyTap> Keyboard::tapsFor(const std::string& text) const {
  std::vector<KeyTap> out;
  int layer = layer_;
  auto find = [&](int l, const std::string& label, Action a) -> int {
    for (size_t i = 0; i < layers_[l].size(); ++i)
      if (layers_[l][i].action == a && (a != Action::Char || layers_[l][i].label == label)) return static_cast<int>(i);
    return -1;
  };
  auto tap = [&](int l, int index, bool sw) {
    auto c = keyCenter(l, static_cast<size_t>(index));
    return KeyTap{c.first, c.second, sw};
  };
  for (size_t i = 0; i < text.size(); ++i) {
    std::string ch(1, text[i]);
    if (ch == " ") {
      out.push_back(tap(layer, find(layer, "", Action::Space), layer == 1));
      if (layer == 1) layer = 0;
      continue;
    }
    // Which layer has the character?
    int want = -1;
    for (int l : {layer, 0, 1, 2}) {
      if (find(l, ch, Action::Char) >= 0) {
        want = l;
        break;
      }
    }
    if (want < 0) continue;  // not on the keyboard
    if (want != layer) {
      if (want == 1) {
        if (layer == 2) {  // symbols -> letters -> shift
          out.push_back(tap(2, find(2, "", Action::Letters), true));
          layer = 0;
        }
        out.push_back(tap(layer, find(layer, "", Action::Shift), true));
        layer = 1;
      } else if (want == 2) {
        out.push_back(tap(layer, find(layer, "", Action::Symbols), true));
        layer = 2;
      } else {  // want == 0
        if (layer == 2) out.push_back(tap(2, find(2, "", Action::Letters), true));
        layer = 0;
      }
    }
    out.push_back(tap(layer, find(layer, ch, Action::Char), layer == 1));  // a capital returns to abc: repaints all keys
    if (layer == 1) layer = 0;
  }
  return out;
}

// ---- TextField

TextField::~TextField() { *alive_ = false; }

void TextField::setText(const std::string& t) {
  std::string v = t;
  if (lines_ == 1) std::replace(v.begin(), v.end(), '\n', ' ');
  if (v == text_) return;
  text_ = v;
  invalidate(Hint::Feedback);
  if (onChange) onChange();
}

size_t TextField::charCount(const std::string& utf8) {
  size_t n = 0;
  for (unsigned char c : utf8)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

std::string TextField::fitted(const std::string& utf8) const {
  if (maxChars_ == 0) return utf8;
  const size_t have = charCount(text_);
  if (have >= maxChars_) return "";
  size_t room = maxChars_ - have, i = 0;
  while (i < utf8.size()) {
    if ((static_cast<unsigned char>(utf8[i]) & 0xC0) != 0x80) {
      if (room == 0) break;
      --room;
    }
    ++i;
  }
  return utf8.substr(0, i);
}

void TextField::append(const std::string& utf8) {
  const std::string add = fitted(utf8);
  if (!add.empty()) setText(text_ + add);
}

void TextField::backspace() {
  if (text_.empty()) return;
  size_t i = text_.size() - 1;
  while (i > 0 && (static_cast<unsigned char>(text_[i]) & 0xC0) == 0x80) --i;
  setText(text_.substr(0, i));
}

bool TextField::paste() {
  const std::string clip = clipboard::get();
  if (clip.empty()) return false;
  const size_t before = text_.size();
  append(clip);
  return text_.size() != before;
}

void TextField::openMenu() {
  Root* r = host();
  if (!r) return;
  auto sheet = std::make_unique<Sheet>();
  Sheet* raw = sheet.get();
  Column* col = sheet->content();
  const bool syncing = clipboard::syncing();
  // A count, never the text (it may be a password).
  auto caption = [](bool checking) {
    const size_t n = charCount(clipboard::get());
    std::string t = n == 0 ? std::string("Clipboard is empty")
                           : std::string(clipboard::source() == clipboard::kFromPhone ? "Clipboard from your phone: " : "Clipboard: ") +
                                 std::to_string(n) + (n == 1 ? " character" : " characters");
    return checking ? t + ", checking your phone..." : t;
  };
  auto* info = col->add<Label>(caption(syncing), Type::Caption, secondary);
  info->maxLines = 2;
  const size_t clipChars = charCount(clipboard::get());
  std::weak_ptr<bool> alive = alive_;
  TextField* self = this;
  auto* paste = col->add<Button>("Paste", [raw, self, alive] {
    raw->close();
    if (!alive.expired()) self->paste();
  }, Button::Kind::Secondary);
  paste->enabled = clipChars > 0;
  if (!masked_) {  // a masked field (a password) is never copied
    auto* copy = col->add<Button>("Copy all", [raw, self, alive, r] {
      raw->close();
      if (alive.expired()) return;
      r->showToast(clipboard::set(self->text(), "field") ? "Copied" : "Could not copy");
    }, Button::Kind::Secondary);
    copy->enabled = !text_.empty();
  }
  auto* clear = col->add<Button>("Clear", [raw, self, alive] {
    raw->close();
    if (!alive.expired()) self->setText("");
  }, Button::Kind::Secondary);
  clear->enabled = !text_.empty();
  r->addOverlay(std::move(sheet));
  if (!syncing) return;
  // The phone may have shared something newer: read the relay, then show what the clipboard holds now.
  clipboard::pull([alive, r, raw, info, paste, caption](bool) {
    if (alive.expired() || !r->hasOverlay(raw)) return;
    info->setText(caption(false));
    const bool on = !clipboard::empty();
    if (paste->enabled != on) {
      paste->enabled = on;
      paste->invalidate(Hint::Content);
    }
  });
}

bool TextField::onTouch(const Gesture& g) {
  if (!clipboardMenu) return false;
  switch (g.type) {
    case Gesture::Down:
    case Gesture::Tap:
    case Gesture::Cancel:
      return true;  // the field takes the touch so that a long press reaches it
    case Gesture::LongPress:
      openMenu();
      return true;
    default:
      return false;  // swipes go on to the parent (a Pager)
  }
}

std::string TextField::shown() const {
  if (!masked_) return text_;
  std::string out;
  for (unsigned char c : text_)
    if ((c & 0xC0) != 0x80) out += "\xE2\x80\xA2";  // bullet
  return out;
}

namespace {
constexpr int kFieldLineH = 32;  // line pitch of a multi-line field (Body, 22 px)
}

Size TextField::measure(int availW) {
  if (lines_ == 1) return {availW, kTouchMin};
  return {availW, lines_ * kFieldLineH + 2 * s2 - 8};
}

void TextField::draw(Canvas& c) {
  strokeRound(c, frame, kRadius, kStroke, ink);
  const int pad = s2;
  const int maxW = frame.w - 2 * pad - 4;
  std::string s = shown();
  if (lines_ > 1) {
    const int top = frame.y + pad - 4;
    if (s.empty()) {
      drawText(c, placeholder_, frame.x + pad, top + kFieldLineH / 2, Type::Body, disabled, Anchor::MiddleLeft, bg());
      c.fillRect(frame.x + pad, top + kFieldLineH / 2 - 13, 2, 26, ink);
      return;
    }
    // Paragraphs at line feeds, each wrapped like a Label; the last `lines_` lines are shown.
    std::vector<std::string> all;
    size_t pos = 0;
    while (true) {
      const size_t e = s.find('\n', pos);
      Label para(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos), Type::Body);
      para.maxLines = 0;
      for (auto& l : para.lines(c, maxW)) all.push_back(std::move(l));
      if (e == std::string::npos) break;
      pos = e + 1;
    }
    const size_t first = all.size() > static_cast<size_t>(lines_) ? all.size() - lines_ : 0;
    int y = top;
    for (size_t i = first; i < all.size(); ++i, y += kFieldLineH)
      drawText(c, all[i], frame.x + pad, y + kFieldLineH / 2, Type::Body, ink, Anchor::MiddleLeft, bg());
    int cx = frame.x + pad + measureText(c, all.back(), Type::Body) + 2;
    if (s.back() == ' ' && !all.back().empty()) cx += measureText(c, " ", Type::Body);
    c.fillRect(std::min(cx, frame.x + frame.w - pad), y - kFieldLineH + kFieldLineH / 2 - 13, 2, 26, ink);  // caret
    return;
  }
  const int cy = frame.y + frame.h / 2;
  if (s.empty()) {
    drawText(c, placeholder_, frame.x + pad, cy, Type::Body, disabled, Anchor::MiddleLeft, bg());
    c.fillRect(frame.x + pad, cy - 16, 2, 32, ink);  // caret
    return;
  }
  // Show the tail that fits.
  while (!s.empty() && measureText(c, s, Type::Title) > maxW) {
    size_t n = 1;
    while (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) ++n;
    s.erase(0, n);
  }
  drawText(c, s, frame.x + pad, cy, Type::Title, ink, Anchor::MiddleLeft, bg());
  c.fillRect(frame.x + pad + measureText(c, s, Type::Title) + 2, cy - 16, 2, 32, ink);
}

void bindKeyboard(Keyboard& kb, TextField& field, std::function<void()> onEnter, size_t maxLen) {
  TextField* f = &field;
  kb.onKey = [f, onEnter = std::move(onEnter), maxLen](Keyboard::Kind kind, const std::string& s) {
    switch (kind) {
      case Keyboard::Kind::Char:
        if (f->text().size() + s.size() <= maxLen) f->append(s);
        break;
      case Keyboard::Kind::Backspace:
        f->backspace();
        break;
      case Keyboard::Kind::Enter:
        if (onEnter) onEnter();
        else if (f->lines() > 1) f->append("\n");  // a multi-line field without an Enter action takes a line feed
        break;
    }
  };
}

}  // namespace pos::ui
