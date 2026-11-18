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

// A swipe up that starts this close to the bottom edge cleans the whole screen (Shell::onGesture).
constexpr int kCleanEdgeH = 60;
// The idle return (config launcher.homeAfterSec) looks again this often while it has no time to wait for: the setting is
// off, or the screen on top stays open. No extra wake-ups: the loop ticks once a minute anyway (kMaxSleepMs).
constexpr uint32_t kIdleHomeRecheckMs = 60000;

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

  // Saves state, leaves the sleep screen on the panel and powers the device off (only the side button starts it again).
  void sleepNow();

  Os& os() { return os_; }
  ui::Root& root() { return root_; }
  ui::StatusBar& statusBar() { return *status_; }
  ui::TitleBar& titleBar() { return *title_; }

  // Surface
  void onGesture(const Gesture& g) override;
  void render(Canvas& canvas, DisplayManager& dm) override;
  bool dirty() const override { return !sleeping_ && (!deferred_.empty() || root_.dirty()); }
  void prepareForPowerOff() override;
  std::string currentAppId() const override { return appId_; }
  std::string debugState() const override { return stack_.empty() ? std::string() : stack_.back().screen->debugState(); }
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
    bool full = false;
  };

  void push(std::unique_ptr<Screen> s, bool launcher);
  void popTop(bool hide = true);  // destroys the top screen (onHide first)
  void defer(std::function<void()> fn) { deferred_.push_back(std::move(fn)); }
  // The same for an action of the running app: dropped if that app has closed by the time its turn comes.
  void deferForApp(std::function<void()> fn) {
    defer([this, run = appRun_, fn = std::move(fn)] {
      if (app_ && run == appRun_) fn();
    });
  }
  void runDeferred();
  void showTop();          // title bar, visibility, onShow
  // Idle return: leaves an app nobody touched for config launcher.homeAfterSec and shows the launcher. Not an app
  // registered with `.stayOpen`, not while the top screen says staysOpen(), a keyboard is showing or a finger is down.
  void idleHomeCheck();
  void updateStatus();

  Os& os_;
  ui::Root root_;
  ui::StatusBar* status_ = nullptr;
  ui::TitleBar* title_ = nullptr;
  ui::Stack* content_ = nullptr;
  std::vector<Entry> stack_;
  std::vector<std::function<void()>> deferred_;
  Timers::Id statusTimer_ = 0;
  bool sleeping_ = false;  // the sleep screen is on the panel

  // The running app.
  std::unique_ptr<App> app_;
  std::string appId_;
  std::unique_ptr<Nav> nav_;
  std::unique_ptr<Store> store_;
  std::unique_ptr<Cache> cache_;
  std::unique_ptr<AppContext> ctx_;
  size_t appBase_ = 0;  // stack index of the app's first screen
  unsigned appRun_ = 0;  // changes whenever an app opens or closes: tells the actions of one run from the next
  bool appStaysOpen_ = false;   // AppInfo::stayOpen of the running app
  uint32_t appOpenedMs_ = 0;    // clock.millis() when it opened: an app opened without a touch (boot, remote) is not idle yet
  Timers::Id idleTimer_ = 0;    // the next idleHomeCheck(), 0 = none
};

}  // namespace pos
