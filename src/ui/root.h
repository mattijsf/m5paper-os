// A widget tree shown on the whole panel (not the app-level pos::Screen of core/app.h): owns the root, lays it out, redraws dirty widgets and
// dispatches gestures. Implements core's Surface so the Os can drive it.
#pragma once

#include <memory>
#include <vector>

#include "core/surface.h"
#include "core/timers.h"
#include "ui/widget.h"

namespace pos::ui {

class Root : public Surface {
 public:
  // The canvas and DisplayManager are the Os's (or a test's) and must outlive the screen.
  // `timers` (optional) lets Toast time out.
  Root(Canvas& canvas, DisplayManager& dm, Timers* timers = nullptr) : canvas_(canvas), dm_(dm), timers_(timers) {}
  ~Root() override;

  Canvas& canvas() { return canvas_; }
  Timers* timers() { return timers_; }
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

  // Overlays are drawn above the root and get touches first: a Sheet (modal, full screen) or a Toast.
  // The overlay is given the full screen, then lays itself out (it may shrink its own frame).
  Widget* addOverlay(std::unique_ptr<Widget> w);
  // Removes an overlay and redraws what was under it. Safe to call from the overlay's own handlers.
  void closeOverlay(Widget* w);
  bool hasOverlay() const { return !overlays_.empty(); }
  // Shows a one-line message at the bottom for `ms` (needs timers), replacing the previous one.
  void showToast(const std::string& text, uint32_t ms = 3000);
  // Redraws a region of the root (and the overlays over it) with `hint`.
  void invalidateRect(Rect r, Hint hint);

  // Called by widgets.
  void markDirty(Widget* w, Hint h);
  void forget(Widget* w);          // a widget is being destroyed
  void forgetTree(Widget* w);      // a subtree is being removed
  Widget* pressed() const { return consumer_; }

 private:
  struct Dirty {
    Widget* w;
    Hint hint;
    bool hasRect = false;  // redraw only `rect` of w (the root)
    Rect rect;
  };
  Widget* overlayOf(Widget* w) const;  // the overlay w belongs to, nullptr if it is in the root tree
  void hideToast();
  bool deliver(Widget* start, const Gesture& g);
  static bool isAncestor(const Widget* a, const Widget* b);  // a above b

  Canvas& canvas_;
  DisplayManager& dm_;
  Timers* timers_;
  std::unique_ptr<Widget> root_;
  std::vector<std::unique_ptr<Widget>> overlays_;
  std::vector<std::unique_ptr<Widget>> retired_;  // closed overlays, destroyed on the next pass
  Widget* toast_ = nullptr;
  Timers::Id toastTimer_ = 0;
  std::vector<Dirty> dirty_;
  Widget* target_ = nullptr;    // deepest widget under the finger at Down
  Widget* consumer_ = nullptr;  // widget that consumed Down
};

}  // namespace pos::ui
