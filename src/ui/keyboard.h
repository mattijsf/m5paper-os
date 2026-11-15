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

// One-line text input box (72 px high). Shows the tail of the text if it does not fit; masked = bullets.
class TextField : public Widget {
 public:
  explicit TextField(std::string placeholder = "") : placeholder_(std::move(placeholder)) {}
  ~TextField() override;
  const std::string& text() const { return text_; }
  void setText(const std::string& t);
  void setMasked(bool m) { masked_ = m; invalidate(Hint::Content); }
  void append(const std::string& utf8);
  void backspace();  // removes the last character (UTF-8 aware)
  Size measure(int availW) override { return {availW, kTouchMin}; }
  void draw(Canvas& c) override;

 private:
  std::string shown() const;
  std::string text_, placeholder_;
  bool masked_ = false;
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
