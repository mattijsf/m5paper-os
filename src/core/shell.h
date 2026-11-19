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
// The idle check (config power.autoOffMin and power.idleShow, DECISIONS.md D38) looks again this often while it has no
// time to wait for. No extra wake-ups: the loop ticks once a minute anyway (kMaxSleepMs).
constexpr uint32_t kIdleRecheckMs = 60000;
// A sleep screen that stays on (on USB) is looked at this often: drawn again when what it shows changed (the date, an
// event that is over, a badge), and the network read every power.refreshHours.
constexpr uint32_t kSleepScreenTickMs = 10u * 60 * 1000;

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
  void prepareForIdleOff() override;
  // The sleep screen is on the panel and the device stays on (idle on USB, power.idleShow): a tap leaves it.
  bool showingSleepScreen() const { return dozing_; }
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
  // Idle check: nobody touched the device for power.autoOffMin (the reader: autoOffMinReader). power.idleShow Home: the
  // open app is left for the launcher (not one registered with `.stayOpen`); SleepScreen: the sleep screen, the device
  // staying on (on battery the PowerPolicy switches it off at the same moment). Not while the top screen says
  // staysOpen(), a keyboard is showing or a finger is down.
  void idleCheck();
  // The sleep screen on the panel. `idle`: nobody asked for it, so the next start resumes only a book or a game.
  // `stayOn`: the device stays on (its footer says "Tap to start").
  void prepareSleep(bool idle, bool stayOn);
  void showSleepScreen();   // idle on USB: the sleep screen, staying on
  void sleepScreenTick();   // see kSleepScreenTickMs
  void redrawSleepScreen();
  void wake();              // a tap on a sleep screen that stays on
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
  bool dozing_ = false;    // ... and the device stays on: a tap leaves it (showSleepScreen)
  bool reading_ = false;   // its network read runs on the worker
  uint32_t lastReadMs_ = 0;
  std::string sleepKey_, sleepUpdated_;  // what the sleep screen on the panel shows (sleepScreenKey), "14:05" of its last read
  Timers::Id sleepTimer_ = 0;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);

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
  Timers::Id idleTimer_ = 0;    // the next idleCheck(), 0 = none
};

}  // namespace pos
