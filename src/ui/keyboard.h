// On-screen keyboard and text field (docs/UI.md "Widgets"): QWERTY, three layers (abc, ABC, 123#),
// keys 54 x 72, four rows = 288 px high, full screen width. A key shows its pressed look (ink) only if held
// longer than kPressDelayMs, and only that key's rectangle is redrawn: a quick tap pushes just the changed
// text field pixels.
#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/surface.h"
#include "ui/widget.h"
#include "ui/text.h"

namespace pos::ui {

class Keyboard : public Widget {
 public:
  enum class Kind : uint8_t { Char, Backspace, Enter };
  Keyboard();
  std::function<void(Kind, const std::string&)> onKey;  // Char: the UTF-8 text of the key

  bool isKeyboard() const override { return true; }
  Size measure(int availW) override { return {availW, kRows * kKeyH}; }
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;

  int layer() const { return layer_; }  // 0 abc, 1 ABC (one letter), 2 123#
  // Screen points to tap, in order, to type `text` starting from the current layer (switching layers as needed;
  // taps that repaint the whole keyboard are flagged).
  std::vector<KeyTap> tapsFor(const std::string& text) const;

  static constexpr int kKeyW = 54, kKeyH = 72, kRows = 4;

 private:
  enum class Action : uint8_t { Char, Shift, Symbols, Letters, Backspace, Space, Enter, None };
  struct Key {
    int x, w, row;
    std::string label;  // what is drawn and typed
    Action action;
    Icon icon;
    bool hasIcon;
  };
  void buildLayer(int layer, std::vector<Key>& keys) const;
  const std::vector<Key>& keys() const { return layers_[layer_]; }
  int keyAt(int px, int py) const;  // index into keys(), -1 if none
  Rect keyRect(const Key& k) const { return Rect{static_cast<int16_t>(frame.x + k.x), static_cast<int16_t>(frame.y + k.row * kKeyH), static_cast<int16_t>(k.w), kKeyH}; }
  void drawKey(Canvas& c, const Key& k, bool pressed);
  void press(int index);
  void release();
  void commit(const Key& k);
  std::pair<int, int> keyCenter(int layer, size_t index) const;

  std::vector<Key> layers_[3];
  int layer_ = 0;
  int pressed_ = -1;
  uint32_t pressSeq_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// Text input box. One line (72 px high, shows the tail of the text if it does not fit; masked = bullets) or, with setLines(n),
// n wrapped lines (line feeds kept; the last n lines are shown). The caret is always at the end.
// A long press opens a sheet with Paste / Copy all / Clear (the OS clipboard, core/clipboard, DECISIONS.md D21): Paste is
// disabled when the clipboard is empty, Copy all and Clear when the field is; a masked field has no Copy all.
class TextField : public Widget {
 public:
  explicit TextField(std::string placeholder = "") : placeholder_(std::move(placeholder)) {}
  ~TextField() override;
  const std::string& text() const { return text_; }
  void setText(const std::string& t);
  void setMasked(bool m) { masked_ = m; invalidate(Hint::Content); }
  // Wrapped lines shown (1 = the one-line field). Line feeds are kept only when lines > 1 (a one-line field turns them into spaces).
  void setLines(int n) { lines_ = n < 1 ? 1 : n; }
  int lines() const { return lines_; }
  // Most characters (code points) the field takes, 0 = no limit. Typing and pasting stop there.
  void setMaxChars(size_t n) { maxChars_ = n; }
  size_t maxChars() const { return maxChars_; }
  void append(const std::string& utf8);  // cut to fit maxChars, at a character boundary
  void backspace();  // removes the last character (UTF-8 aware)
  // Appends the clipboard (cut to fit). False if the clipboard is empty or nothing fitted.
  bool paste();
  // Called after every change of the text (typing, paste, clear, setText).
  std::function<void()> onChange;
  bool clipboardMenu = true;  // false: no long-press sheet
  // Opens the Paste / Copy all / Clear sheet (what a long press does).
  void openMenu();

  Size measure(int availW) override;
  void draw(Canvas& c) override;
  bool onTouch(const Gesture& g) override;

  static size_t charCount(const std::string& utf8);  // code points

 private:
  std::string shown() const;
  std::string fitted(const std::string& utf8) const;  // the part of `utf8` that still fits
  std::string text_, placeholder_;
  bool masked_ = false;
  int lines_ = 1;
  size_t maxChars_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

// The keyboard as a full-width overlay at the bottom of the screen (screens whose content column has side margins):
// Root::addOverlay(std::make_unique<KeyboardPanel>()). Touches outside the keyboard reach the screen below.
class KeyboardPanel : public Widget {
 public:
  KeyboardPanel() { kb_ = add<Keyboard>(); }
  Keyboard* keyboard() const { return kb_; }
  void layout() override {
    frame = Rect{0, static_cast<int16_t>(kScreenH - Keyboard::kKeyH * Keyboard::kRows), kScreenW, static_cast<int16_t>(Keyboard::kKeyH * Keyboard::kRows)};
    kb_->frame = frame;
  }

 private:
  Keyboard* kb_;
};

// Connects a keyboard to a text field (typing, backspace); Enter calls `onEnter`.
void bindKeyboard(Keyboard& kb, TextField& field, std::function<void()> onEnter = {}, size_t maxLen = 128);

}  // namespace pos::ui
