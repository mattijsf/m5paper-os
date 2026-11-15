// Widget base (docs/UI.md "Widgets"). A tree of widgets draws into the canvas; widgets mark
// themselves dirty with invalidate(), the Root redraws only dirty widgets once per loop pass and
// hands the rectangles to the DisplayManager.
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "core/display_manager.h"
#include "core/gesture.h"
#include "core/gfx.h"
#include "ui/theme.h"

namespace pos::ui {

class Root;

struct Size {
  int w = 0, h = 0;
};

class Widget {
 public:
  virtual ~Widget();
  Widget() = default;
  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  Rect frame;           // set by the parent's layout()
  int flex = 0;         // > 0: takes leftover space along the parent's main axis, in proportion
  bool visible = true;  // invisible widgets take no space, draw nothing and get no touches
  bool touchable = true;     // false: touches fall through to what is below (Toast)
  bool modal = false;        // overlays only: touches that hit nothing else go to this widget (Sheet)
  bool transparent = false;  // true: the frame is not cleared before drawing (overlays that draw over the screen)

  // Preferred size when at most `availW` wide.
  virtual Size measure(int /*availW*/) { return {}; }
  // Position the children inside `frame` (containers).
  virtual void layout() {}
  // Draw this widget (not its children) into the canvas; the frame is already cleared to bg() and clipped.
  virtual void draw(Canvas& /*c*/) {}
  // Gestures. Return true to consume. Down goes to the deepest widget under the finger and bubbles up
  // until consumed; the consumer then gets the rest of that touch (Tap, LongPress, Cancel), and
  // Swipe* bubbles from there.
  virtual bool onTouch(const Gesture& /*g*/) { return false; }
  // The device build has no RTTI: widgets that must be found by kind say so.
  virtual bool isKeyboard() const { return false; }

  // Marks the frame dirty. Cheap; the Root redraws and commits once per loop pass.
  void invalidate(Hint h = Hint::Content);
  // Same for part of the frame (a key of the keyboard): only `r` is cleared, redrawn and pushed.
  void invalidateRect(Rect r, Hint h = Hint::Feedback);

  // Tree
  template <class T, class... Args>
  T* add(Args&&... args) {
    auto w = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = w.get();
    adopt(std::move(w));
    return raw;
  }
  Widget* adopt(std::unique_ptr<Widget> child);
  std::unique_ptr<Widget> remove(Widget* child);  // gives the child back; nothing is redrawn by itself
  Widget* parent() const { return parent_; }
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }
  Root* host() const;
  Canvas* canvas() const;

  // Background colour behind this widget's content: its own if set, else the parent's, else paper.
  Color bg() const;
  void setBg(Color c) { bg_ = c; hasBg_ = true; }

  bool contains(int x, int y) const { return x >= frame.x && x < frame.x + frame.w && y >= frame.y && y < frame.y + frame.h; }
  // Deepest visible widget containing the point (this if no child does), nullptr if outside.
  Widget* hit(int x, int y);
  // Draws this widget, then its children, everything clipped to `clip` (and each child to its own
  // frame). Used by the Root.
  void drawTree(Canvas& c, Rect clip);

 private:
  friend class Root;
  Widget* parent_ = nullptr;
  Root* screen_ = nullptr;  // set on the root by Root
  std::vector<std::unique_ptr<Widget>> children_;
  Color bg_ = 0;
  bool hasBg_ = false;
};

}  // namespace pos::ui
