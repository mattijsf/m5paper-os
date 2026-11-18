// App framework (docs/APPS.md is the contract). An app is a class derived from App, registered with
// POS_REGISTER_APP; it pushes Screens (build(root) creates widgets) through ctx.nav.
//
// Note: pos::Screen here is the app-level screen; the widget surface of the whole panel is ui::Root.
#pragma once

#include <cstdarg>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/config.h"
#include "core/ha.h"
#include "core/log.h"
#include "core/net.h"
#include "core/secrets.h"
#include "core/store.h"
#include "core/timers.h"
#include "core/worker.h"
#include "gen/icons.h"
#include "hal/clock.h"
#include "hal/net.h"
#include "ui/layout.h"

namespace pos {

using Icon = gen::Icon;

class AppContext;
class Nav;
class Os;
class Shell;

// One screen of an app (or the launcher). build() runs once per instance and fills `root`, a Column
// with 24 px side margins; onShow()/onHide() run each time the screen becomes visible / is covered.
class Screen {
 public:
  virtual ~Screen() = default;
  virtual void build(ui::Column& root) = 0;
  virtual void onShow() {}
  virtual void onHide() {}
  // True: the screen gets the whole panel (no status bar, no title bar, no side margins). Read before build().
  virtual bool fullScreen() const { return false; }
  // Space left and right of the content, for a screen with the bars. Teletekst's grid of 40 columns needs 520 px of the 540.
  // Read before build().
  virtual int sideMargin() const { return ui::kMargin; }
  // One line describing what is shown ("page=3/120"), for UI scripts (expect-state, page-through). "" = nothing to say.
  virtual std::string debugState() const { return ""; }
  // True: while this screen is on top, the app is not left for the launcher when nobody touches it (the idle return,
  // config launcher.homeAfterSec). For a screen that is in use without a finger: a pairing code being compared with the
  // phone, files arriving over Wi-Fi. Asked each time the idle return looks, so it may change.
  virtual bool staysOpen() const { return false; }

  // Title shown in the title bar while this screen is on top. Call from build() or later.
  void setTitle(const std::string& title);
  const std::string& title() const { return title_; }
  // An icon button at the right end of the title bar while this screen is on top (a refresh button, say).
  void setAction(Icon icon, std::function<void()> fn);
  bool hasAction() const { return static_cast<bool>(actionFn_); }
  // A small picture in the title bar left of the action button while this screen is on top (Somdoku's score tiles):
  // `width` px wide, drawn by `paint` into its frame. width 0 / no paint: none. refreshTitleAccessory() redraws only it.
  void setTitleAccessory(int width, std::function<void(Canvas&, const Rect&)> paint);
  void refreshTitleAccessory();

 protected:
  AppContext& ctx() const { return *ctx_; }

 private:
  friend class Shell;
  friend class Nav;
  AppContext* ctx_ = nullptr;
  Shell* shell_ = nullptr;
  std::string title_;
  Icon actionIcon_ = Icon::RefreshCw;
  std::function<void()> actionFn_;
  int accessoryW_ = 0;
  std::function<void(Canvas&, const Rect&)> accessoryFn_;
};

// What the sleep screen shows about the app being left (docs/POWER.md "Sleep screen"): a title with a picture and a progress,
// like the book being read.
struct SleepInfo {
  std::string title, subtitle;
  int percent = -1;   // < 0: no progress bar
  std::string image;  // virtual path of a JPEG or PNG, "" = none
};

class App {
 public:
  virtual ~App() = default;
  // Asked just before power-off, while the app is still open. True if `info` was filled.
  virtual bool sleepInfo(SleepInfo& /*info*/) { return false; }
  // The app was opened: push the first screen.
  virtual void onStart(AppContext& ctx) = 0;
  // The app is being left (back from its first screen, home, power-off): save state. Not called
  // again; the App and its screens are destroyed afterwards.
  virtual void onStop() {}
};

// Registration data. Field order matters (designated initialisers).
struct AppInfo {
  const char* id;    // unique, lowercase; also the config and cache namespace
  const char* name;  // label under the launcher tile
  Icon icon;
  int order;         // position in the launcher (lower first)
  // True: the app is never left for the launcher by itself when nobody touches it (the idle return, config
  // launcher.homeAfterSec): something one looks at for minutes without a touch, a book or a game.
  bool stayOpen = false;
};

struct AppEntry {
  AppInfo info;
  std::unique_ptr<App> (*create)();
};

// The static registry the launcher reads. Sorted by `order`, then id.
class AppRegistry {
 public:
  static AppRegistry& instance();
  bool add(const AppInfo& info, std::unique_ptr<App> (*create)());
  std::vector<const AppEntry*> sorted() const;
  const AppEntry* find(const std::string& id) const;

 private:
  std::vector<AppEntry> entries_;
};

template <class A>
std::unique_ptr<App> createApp() {
  return std::make_unique<A>();
}

#define POS_CONCAT_(a, b) a##b
#define POS_CONCAT(a, b) POS_CONCAT_(a, b)
// POS_REGISTER_APP(HelloApp, { .id = "hello", .name = "Hello", .icon = Icon::Info, .order = 90 });
#define POS_REGISTER_APP(Class, ...)                                                                              \
  static const bool POS_CONCAT(kPosAppRegistered_, __LINE__) =                                                    \
      ::pos::AppRegistry::instance().add(::pos::AppInfo __VA_ARGS__, &::pos::createApp<Class>)

// Navigation inside the running app. push/pop/home are applied after the current gesture or
// callback returns (never while a widget handler is still running).
class Nav {
 public:
  template <class S, class... Args>
  void push(Args&&... args) {
    pushScreen(std::make_unique<S>(std::forward<Args>(args)...));
  }
  void pop();   // back; from the first screen it leaves the app
  void home();  // leave the app, show the launcher

 private:
  friend class Shell;
  explicit Nav(Shell& shell) : shell_(shell) {}
  void pushScreen(std::unique_ptr<Screen> s);
  Shell& shell_;
};

// What an app gets (docs/APPS.md "What an app gets"). `config` and `secrets` arrive with the config service.
class AppContext {
 public:
  Nav& nav;
  Store& store;
  Cache& cache;
  hal::Clock& clock;
  const Config& config;      // system settings, read-only
  const Secrets& secrets;    // secrets by key; never log or display the values

  // Runs `job` on the worker thread and `onDone(result)` on the UI thread; dropped if the screen
  // that was on top when this was called is gone by then.
  template <class Job, class Done>
  void async(Job job, Done onDone) {
    worker().async(std::move(job), [alive = aliveToken(), onDone = std::move(onDone)](auto&&... r) mutable {
      if (alive.expired()) return;
      onDone(std::forward<decltype(r)>(r)...);
    });
  }
  // Runs `job` on the worker thread and expects nothing back. Unlike async() it runs even if the screen or the whole
  // app is closed before its turn: for what must happen all the same (save a file, switch something off again).
  // Capture by value; nothing of the screen (`this`, widgets, `ctx`) may be used in it.
  template <class Job>
  void background(Job job) {
    worker().post(std::move(job));
  }
  // async over NetService::perform: brings Wi-Fi up if needed (known networks, one fast attempt + one scan), sends the
  // request, keeps Wi-Fi up for net.lingerSec. `onDone(hal::HttpResponse)`: status 0 and error "No Wi-Fi: ..." if offline.
  template <class Done>
  void http(hal::HttpRequest request, Done onDone) {
    NetService* net = &netService();
    async([net, request = std::move(request)] { return net->perform(request); }, std::move(onDone));
  }
  // For a job on the worker (ctx.async) that makes several requests in a row: what it calls to send one (NetService::perform:
  // Wi-Fi on demand, the linger policy). One job that reads everything a screen needs gives one update of the screen
  // (docs/APPS.md "Threads"). Blocking: never call it on the UI thread.
  std::function<hal::HttpResponse(const hal::HttpRequest&)> transport();
  // A Home Assistant client for jobs on the worker (ctx.async): HA_URL / HA_TOKEN from the card, requests through the net service
  // (Wi-Fi on demand). Blocking calls: never on the UI thread. docs/BACKEND.md.
  HaClient ha();
  // Sets the clock from the internet when it is lost (battery ran flat): joins Wi-Fi once (one fast attempt, one scan) and
  // waits up to 12 s for the time sync that follows every connect while the clock is unset. `done(ok, error)` on the UI
  // thread; error is the net service's ("No known network in range", ...) or "No time from the network". Not while the
  // clock is set (done(true) at once). For apps that cannot work without the date (Somdoku, the word game).
  void syncClock(std::function<void(bool ok, const std::string& error)> done);
  static constexpr const char* kNoNetworkTime = "No time from the network";  // syncClock's error when Wi-Fi came up but no time
  // Counts as use of the connection: Wi-Fi, when it is up, stays up for another net.lingerSec (it is not brought up).
  // For a screen whose next tap will most likely be a request (Teletekst: the next page); bounded by the caller.
  void keepWifi();
  // One-shot timer on the UI thread; cancelled when the screen that was on top is destroyed.
  Timers::Id timer(uint32_t ms, std::function<void()> fn);
  void toast(const std::string& text);
  // POS_LOG with the app id as tag.
  void log(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  const std::string& appId() const { return id_; }
  // The Os: for built-in system apps only (Settings changes config, cleans the panel). Other apps use the members above.
  Os& os();

 private:
  friend class Shell;
  AppContext(Shell& shell, Nav& nav, Store& store, Cache& cache, hal::Clock& clock, const Config& config, const Secrets& secrets, std::string id)
      : nav(nav), store(store), cache(cache), clock(clock), config(config), secrets(secrets), shell_(shell), id_(std::move(id)) {}
  Worker& worker();
  NetService& netService();
  std::weak_ptr<bool> aliveToken();
  Shell& shell_;
  std::string id_;
};

}  // namespace pos
