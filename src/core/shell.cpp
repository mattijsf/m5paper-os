#include "core/shell.h"

#include <ctime>

#include "core/timefmt.h"

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

void Nav::pushScreen(std::unique_ptr<Screen> s) {
  Screen* raw = s.release();
  shell_.defer([this, raw] { shell_.push(std::unique_ptr<Screen>(raw), false); });
}
void Nav::pop() {
  shell_.defer([this] {
    if (shell_.depth() <= shell_.appBase_ + 1) shell_.closeApp();
    else shell_.popTop(), shell_.showTop();
  });
}
void Nav::home() {
  shell_.defer([this] { shell_.closeApp(); });
}
size_t Nav::depth() const { return shell_.appOpen() ? shell_.depth() - shell_.appBase_ : 0; }
Screen* Nav::top() const { return shell_.appOpen() ? shell_.top() : nullptr; }

Os& AppContext::os() { return shell_.os(); }
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
  statusTimer_ = os_.timers().every(60000, [this] { updateStatus(); });
}

Shell::~Shell() {
  os_.net().onChange = nullptr;
  os_.timers().cancel(statusTimer_);
  if (app_) app_->onStop();
  // The widget tree dies with root_; screens must go first (their widgets may be referenced).
  while (!stack_.empty()) stack_.pop_back();
}

void Shell::updateStatus() {
  hal::Platform& p = os_.platform();
  const int64_t now = p.clock.now();
  if (now <= 0) {
    status_->setTime("--:--");
  } else {
    struct tm tmv;
    localTime(now, tmv);  // net.timezone
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    status_->setTime(buf);
  }
  status_->setBattery(p.power.batteryPercent(), p.power.charging());
  status_->setWifi(p.net.wifiConnected());
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
  auto* pad = content_->add<ui::Padding>(ui::kMargin, 0, ui::kMargin, 0);
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
  title_->visible = !e.launcher;
  title_->setTitle(e.screen->title());
  title_->clearAction();
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
  app_.reset();
  ctx_.reset();
  nav_.reset();
  cache_.reset();
  store_.reset();
  appId_.clear();
  showTop();
}

Shell::Entry* Shell::entryOf(const Screen* s) {
  for (auto& e : stack_)
    if (e.screen.get() == s) return &e;
  return nullptr;
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
  // Remember what was open (the launcher can resume it), then leave the app so it saves its state.
  Store sys("/data/system.json");
  sys.set("lastApp", appId_);
  sys.save();
  if (app_) closeApp();
}

void Shell::onGesture(const Gesture& g) {
  root_.onGesture(g);
  runDeferred();
}

void Shell::render(Canvas& canvas, DisplayManager& dm) {
  runDeferred();
  root_.render(canvas, dm);
}

}  // namespace pos
