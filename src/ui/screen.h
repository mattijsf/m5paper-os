// A widget tree shown on the whole panel: owns the root, lays it out, redraws dirty widgets and
// dispatches gestures. Implements core's Surface so the Os can drive it.
#pragma once

#include <memory>
#include <vector>

#include "core/surface.h"
#include "ui/widget.h"

namespace pos::ui {

class Screen : public Surface {
 public:
  // The canvas and DisplayManager are the Os's (or a test's) and must outlive the screen.
  Screen(Canvas& canvas, DisplayManager& dm) : canvas_(canvas), dm_(dm) {}
  ~Screen() override;

  Canvas& canvas() { return canvas_; }
  DisplayManager& displayManager() { return dm_; }

  // Takes the root, gives it the full screen and lays it out. Marks everything dirty (Screen hint).
  void setRoot(std::unique_ptr<Widget> root);
  Widget* root() { return root_.get(); }
  // Lays the tree out again (after children were added or content sizes changed) and redraws all.
  void relayout();

  // Surface
  void onGesture(const Gesture& g) override;
  void render(Canvas& canvas, DisplayManager& dm) override;
  bool dirty() const override { return !dirty_.empty(); }

  // Called by widgets.
  void markDirty(Widget* w, Hint h);
  void forget(Widget* w);          // a widget is being destroyed
  void forgetTree(Widget* w);      // a subtree is being removed
  Widget* pressed() const { return consumer_; }

 private:
  struct Dirty {
    Widget* w;
    Hint hint;
  };
  bool deliver(Widget* start, const Gesture& g);
  static bool isAncestor(const Widget* a, const Widget* b);  // a above b

  Canvas& canvas_;
  DisplayManager& dm_;
  std::unique_ptr<Widget> root_;
  std::vector<Dirty> dirty_;
  Widget* target_ = nullptr;    // deepest widget under the finger at Down
  Widget* consumer_ = nullptr;  // widget that consumed Down
};

}  // namespace pos::ui
