#include "core/shell.h"

#include <ctime>

#include "core/quick_panel.h"
#include "core/sleep_screen.h"
#include "core/timefmt.h"
#include "ui/image.h"

#include "ui/widgets.h"

namespace pos {

// ---- registry

AppRegistry& AppRegistry::instance() {
  static AppRegistry r;
  return r;
}

bool AppRegistry::add(const AppInfo& info, std::unique_ptr<App> (*create)()) {
  entries_.push_back(AppEntry{info, create});
  return true;
}

std::vector<const AppEntry*> AppRegistry::sorted() const {
  std::vector<const AppEntry*> out;
  for (const auto& e : entries_) out.push_back(&e);
  std::sort(out.begin(), out.end(), [](const AppEntry* a, const AppEntry* b) {
    if (a->info.order != b->info.order) return a->info.order < b->info.order;
    return std::string(a->info.id) < b->info.id;
  });
  return out;
}

const AppEntry* AppRegistry::find(const std::string& id) const {
  for (const auto& e : entries_)
    if (id == e.info.id) return &e;
  return nullptr;
}

// ---- Screen, Nav, AppContext

void Screen::setTitle(const std::string& title) {
  if (title == title_) return;
  title_ = title;
  if (shell_ && shell_->top() == this) shell_->titleBar().setTitle(title);
}

void Screen::setAction(Icon icon, std::function<void()> fn) {
  actionIcon_ = icon;
  actionFn_ = std::move(fn);
  if (shell_ && shell_->top() == this) {
    if (actionFn_) shell_->titleBar().setAction(actionIcon_, actionFn_);
    else shell_->titleBar().clearAction();
  }
}

void Screen::setTitleAccessory(int width, std::function<void(Canvas&, const Rect&)> paint) {
  accessoryW_ = paint ? width : 0;
  accessoryFn_ = accessoryW_ > 0 ? std::move(paint) : nullptr;
  if (shell_ && shell_->top() == this) {
    if (accessoryFn_) shell_->titleBar().setAccessory(accessoryW_, accessoryFn_);
    else shell_->titleBar().clearAccessory();
  }
}

void Screen::refreshTitleAccessory() {
  if (shell_ && shell_->top() == this && accessoryFn_) shell_->titleBar().refreshAccessory();
}

// The queued actions hold the Shell, not this Nav: the Nav is gone once its app closes, and an action queued behind the
// one that closed it (pop() twice) must find that out instead of running through it (Shell::deferForApp).
void Nav::pushScreen(std::unique_ptr<Screen> s) {
  Shell* shell = &shell_;
  // Shared so the action can be copied; a screen whose push never runs is destroyed with it.
  auto held = std::make_shared<std::unique_ptr<Screen>>(std::move(s));
  shell->deferForApp([shell, held] { shell->push(std::move(*held), false); });
}
void Nav::pop() {
  Shell* shell = &shell_;
  shell->deferForApp([shell] {
    if (shell->depth() <= shell->appBase_ + 1) {
      shell->closeApp();
    } else {
      shell->popTop();
      shell->showTop();
    }
  });
}
void Nav::home() {
  Shell* shell = &shell_;
  shell->deferForApp([shell] { shell->closeApp(); });
}

Os& AppContext::os() { return shell_.os(); }
HaClient AppContext::ha() { return HaClient::fromNet(shell_.os().net(), shell_.os().secrets()); }
Worker& AppContext::worker() { return shell_.os().worker(); }
NetService& AppContext::netService() { return shell_.os().net(); }
std::weak_ptr<bool> AppContext::aliveToken() {
  auto* e = shell_.stack_.empty() ? nullptr : &shell_.stack_.back();
  return e ? std::weak_ptr<bool>(e->alive) : std::weak_ptr<bool>();
}
Timers::Id AppContext::timer(uint32_t ms, std::function<void()> fn) {
  Shell::Entry* e = shell_.stack_.empty() ? nullptr : &shell_.stack_.back();
  Timers::Id id = shell_.os().timers().after(ms, std::move(fn));
  if (e) e->timers.push_back(id);
  return id;
}
namespace {

struct ClockWait {
  std::function<void(bool, const std::string&)> done;
  std::weak_ptr<bool> asker;  // the screen that asked: the wait ends with it, whatever is on top meanwhile
  int left = 48;              // x 250 ms = 12 s
};

// After the connect the sync runs as its own worker job (NetService::tick): look at the clock until it lands.
void pollClock(AppContext* ctx, Timers& timers, std::shared_ptr<ClockWait> w) {
  if (w->asker.expired()) return;  // (the app context may be gone with it: nothing of it is touched before this line)
  if (clockIsSet(ctx->clock.now())) {
    w->done(true, "");
  } else if (w->left-- <= 0) {
    w->done(false, AppContext::kNoNetworkTime);
  } else {
    // Not AppContext::timer(): that one belongs to the screen on top, and a screen pushed over the asker and popped
    // again would take the wait with it.
    timers.after(250, [ctx, &timers, w] { pollClock(ctx, timers, w); });
  }
}
}  // namespace

void AppContext::syncClock(std::function<void(bool, const std::string&)> done) {
  if (clockIsSet(clock.now())) {
    done(true, "");
    return;
  }
  auto w = std::make_shared<ClockWait>();
  w->done = std::move(done);
  w->asker = aliveToken();
  NetService* net = &netService();
  Timers* timers = &shell_.os().timers();
  async([net] { return net->ensureConnected(); }, [this, timers, w](NetService::Result r) {
    if (!r.ok) w->done(false, r.error.empty() ? std::string("No Wi-Fi") : r.error);
    else pollClock(this, *timers, w);
  });
}

void AppContext::toast(const std::string& text) { shell_.root().showToast(text); }
void AppContext::log(const char* fmt, ...) {
  char buf[200];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  POS_LOG(id_.c_str(), "%s", buf);
}

// ---- Shell

Shell::Shell(Os& os) : os_(os), root_(os.canvas(), os.displayManager(), &os.timers()) {
  auto top = std::make_unique<ui::Column>(0);
  status_ = top->add<ui::StatusBar>();
  title_ = top->add<ui::TitleBar>();
  title_->visible = false;
  content_ = top->add<ui::Stack>();
  content_->flex = 1;
  title_->onBack = [this] {
    if (appOpen()) nav_->pop();
  };
  title_->onHome = [this] { closeApp(); };
  root_.setRoot(std::move(top));
  updateStatus();
  os_.net().onChange = [this] { updateStatus(); };  // Wi-Fi came up or went off, the clock was synced
  os_.net().onChangeOwner = this;
  statusTimer_ = os_.timers().every(60000, [this] { updateStatus(); });
}

Shell::~Shell() {
  if (os_.net().onChangeOwner == this) {  // the new Shell of a screen change registered itself before this one died
    os_.net().onChange = nullptr;
    os_.net().onChangeOwner = nullptr;
  }
  os_.timers().cancel(statusTimer_);
  if (app_) app_->onStop();
  // The widget tree dies with root_; screens must go first (their widgets may be referenced).
  while (!stack_.empty()) stack_.pop_back();
}

void Shell::updateStatus() {
  hal::Platform& p = os_.platform();
  const int64_t now = p.clock.now();
  status_->setTime(now > 0 ? clockText(now) : "--:--");  // local time (net.timezone)
  status_->setBattery(p.power.batteryPercent(), p.power.charging());
  status_->setWifi(os_.net().online());
}

void Shell::setLauncher(const LauncherFactory& make) {
  push(make(*this), true);
}

void Shell::push(std::unique_ptr<Screen> s, bool launcher) {
  if (!stack_.empty()) {
    stack_.back().screen->onHide();
    stack_.back().wrapper->visible = false;
  }
  Entry e;
  e.launcher = launcher;
  e.full = s->fullScreen();
  const int margin = e.full ? 0 : ui::kMargin;
  auto* pad = content_->add<ui::Padding>(margin, 0, margin, 0);
  auto* col = pad->add<ui::Column>(0);
  e.wrapper = pad;
  s->shell_ = this;
  s->ctx_ = ctx_.get();
  e.screen = std::move(s);
  stack_.push_back(std::move(e));
  stack_.back().screen->build(*col);
  showTop();
}

void Shell::showTop() {
  Entry& e = stack_.back();
  e.wrapper->visible = true;
  title_->visible = !e.launcher && !e.full;
  status_->visible = !e.full;
  title_->setTitle(e.screen->title());
  title_->clearAction();
  if (e.screen->hasAction()) title_->setAction(e.screen->actionIcon_, e.screen->actionFn_);
  title_->clearAccessory();
  if (e.screen->accessoryFn_) title_->setAccessory(e.screen->accessoryW_, e.screen->accessoryFn_);
  e.screen->onShow();
  root_.relayout();  // whole panel: navigation is a Hint::Screen change
}

void Shell::popTop(bool hide) {
  Entry& e = stack_.back();
  if (hide) e.screen->onHide();
  *e.alive = false;
  for (auto id : e.timers) os_.timers().cancel(id);
  content_->remove(e.wrapper);  // destroys the screen's widgets
  e.screen.reset();
  stack_.pop_back();
}

bool Shell::openApp(const std::string& id) {
  if (app_) return false;
  const AppEntry* entry = AppRegistry::instance().find(id);
  if (!entry) return false;
  appId_ = id;
  store_ = std::make_unique<Store>("/data/apps/" + id + ".json");
  cache_ = std::make_unique<Cache>(id);
  nav_.reset(new Nav(*this));
  ctx_.reset(new AppContext(*this, *nav_, *store_, *cache_, os_.platform().clock, os_.config(), os_.secrets(), id));
  appBase_ = stack_.size();
  ++appRun_;
  app_ = entry->create();
  app_->onStart(*ctx_);
  runDeferred();  // the first screen push
  return true;
}

void Shell::closeApp() {
  if (!app_) return;
  if (stack_.size() > appBase_) stack_.back().screen->onHide();  // lifecycle: onHide, then onStop
  app_->onStop();
  store_->save();
  while (stack_.size() > appBase_) popTop(false);
  os_.worker().cancelAll();  // results of this app's jobs are dropped
  ++appRun_;                 // and so are its queued navigation actions (deferForApp)
  app_.reset();
  ctx_.reset();
  nav_.reset();
  cache_.reset();
  store_.reset();
  appId_.clear();
  showTop();
}

void Shell::runDeferred() {
  // Actions may queue more actions; run until the queue is empty.
  for (int guard = 0; !deferred_.empty() && guard < 64; ++guard) {
    auto work = std::move(deferred_);
    deferred_.clear();
    for (auto& fn : work) fn();
  }
}

void Shell::prepareForPowerOff() {
  if (sleeping_) return;
  os_.flushConfig();  // a setting changed in the last second (Os::saveConfigSoon)
  // Remember what was open (the next boot resumes it), ask the app what the sleep screen should show, then leave the app so it
  // saves its state, and leave the sleep screen on the panel.
  Store sys("/data/system.json");
  sys.set("lastApp", appId_);
  sys.save();
  SleepInfo info;
  const bool haveInfo = app_ && app_->sleepInfo(info);
  if (app_) closeApp();
  std::shared_ptr<Canvas> image;
  if (haveInfo && !info.image.empty()) image = ui::loadImageFit(info.image, kSleepImageW, kSleepImageH);
  sleeping_ = true;  // nothing else draws over the sleep screen
  saveSleepInfo(haveInfo ? &info : nullptr);  // a scheduled refresh redraws the same screen with a new summary
  drawSleepScreen(os_.canvas(), haveInfo ? &info : nullptr, image.get(), sleepNote(cachedUnread(), ""),
                  sleepAgendaLine(os_.platform().clock.now()));
  os_.displayManager().forceClean();
}

void Shell::sleepNow() {
  prepareForPowerOff();
  os_.powerOffForSleep();
}

void Shell::onGesture(const Gesture& g) {
  // A swipe down that started in the status bar opens the quick panel (not on full-screen pages, which have no status bar).
  if (g.type == Gesture::SwipeDown && g.startY < ui::kStatusBarH && status_->visible && !sleeping_ && !root_.hasOverlay()) {
    openQuickPanel(*this);
    runDeferred();
    return;
  }
  // A swipe up that started at the bottom edge cleans the whole screen (one Quality pass), on every screen, full-screen
  // reader pages included: "wipe the panel". The side button cannot do it: the firmware cannot read it (HARDWARE.md).
  if (g.type == Gesture::SwipeUp && g.startY >= kScreenH - kCleanEdgeH && !sleeping_) {
    POS_LOG("shell", "clean (swipe up from the bottom edge)");
    os_.displayManager().forceClean();
    return;
  }
  root_.onGesture(g);
  runDeferred();
}

void Shell::render(Canvas& canvas, DisplayManager& dm) {
  if (sleeping_) return;
  runDeferred();
  root_.render(canvas, dm);
}

}  // namespace pos
