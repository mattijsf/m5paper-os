// The Shell is the root of navigation: status bar, title bar and a stack of screens (the launcher at
// the bottom, the running app's screens above). It is the Surface the Os shows and feeds gestures to.
// docs/APPS.md "Lifecycle", docs/apps/launcher.md.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/app.h"
#include "core/os.h"
#include "core/surface.h"
#include "ui/chrome.h"
#include "ui/root.h"

namespace pos {

class Shell : public Surface {
 public:
  explicit Shell(Os& os);
  ~Shell() override;

  // Builds the launcher screen (the base of the stack). Called once, before the first render.
  using LauncherFactory = std::function<std::unique_ptr<Screen>(Shell&)>;
  void setLauncher(const LauncherFactory& make);

  // Opens a registered app by id (no-op if unknown or one is already open).
  bool openApp(const std::string& id);
  // Same, after the current gesture or callback returns (use this from widget handlers).
  void requestOpen(const std::string& id) { defer([this, id] { openApp(id); }); }
  // Leaves the running app (onStop, destroy) and shows the launcher.
  void closeApp();
  bool appOpen() const { return app_ != nullptr; }
  const std::string& appId() const { return appId_; }
  // Screens on the stack including the launcher.
  size_t depth() const { return stack_.size(); }
  Screen* top() const { return stack_.empty() ? nullptr : stack_.back().screen.get(); }

  Os& os() { return os_; }
  ui::Root& root() { return root_; }
  ui::StatusBar& statusBar() { return *status_; }
  ui::TitleBar& titleBar() { return *title_; }

  // Surface
  void onGesture(const Gesture& g) override;
  void render(Canvas& canvas, DisplayManager& dm) override;
  bool dirty() const override { return !deferred_.empty() || root_.dirty(); }
  std::vector<KeyTap> keyboardTaps(const std::string& text) override { return root_.keyboardTaps(text); }

 private:
  friend class Nav;
  friend class AppContext;
  friend class Screen;
  struct Entry {
    std::unique_ptr<Screen> screen;
    ui::Widget* wrapper = nullptr;  // Padding in the content stack, owns the screen's Column
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::vector<Timers::Id> timers;
    bool launcher = false;
  };

  void push(std::unique_ptr<Screen> s, bool launcher);
  void popTop(bool hide = true);  // destroys the top screen (onHide first)
  void defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }
  void runDeferred();
  void showTop();          // title bar, visibility, onShow
  void updateStatus();
  Entry* entryOf(const Screen* s);

  Os& os_;
  ui::Root root_;
  ui::StatusBar* status_ = nullptr;
  ui::TitleBar* title_ = nullptr;
  ui::Stack* content_ = nullptr;
  std::vector<Entry> stack_;
  std::vector<std::function<void()>> deferred_;
  Timers::Id statusTimer_ = 0;

  // The running app.
  std::unique_ptr<App> app_;
  std::string appId_;
  std::unique_ptr<Nav> nav_;
  std::unique_ptr<Store> store_;
  std::unique_ptr<Cache> cache_;
  std::unique_ptr<AppContext> ctx_;
  size_t appBase_ = 0;  // stack index of the app's first screen
};

}  // namespace pos
