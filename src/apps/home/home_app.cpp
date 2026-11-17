// Home: a remote control for the house (docs/apps/home.md). Three tabs from home.json (lights with scenes, climate, more).
// States are read per entity on the worker (non-admin API subset), shown from a saved copy first; actions update the screen at once
// and are sent afterwards, the tab is read again 1.5 s later, a failed call reverts the row and says why.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <memory>

#include "apps/home/home_model.h"
#include "core/app.h"
#include "core/fs.h"
#include "core/os.h"
#include "core/timefmt.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::home {

namespace {

using ui::Type;

constexpr const char* kCacheFile = "/data/cache/home/states.json";
constexpr int kRowsPerPage = 5;
constexpr uint32_t kRefreshAfterActionMs = 1500;
constexpr uint32_t kPollMs = 30000;
constexpr uint32_t kActiveWindowMs = 2 * 60 * 1000;


std::string celsiusText(double v) {
  char buf[24];
  std::snprintf(buf, sizeof buf, "%.1f \xC2\xB0" "C", v);
  return buf;
}

class HomeScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Home");
    std::string note;
    config_ = loadConfig(note);
    std::string cached;
    if (fs::readFile(kCacheFile, cached)) {
      states_ = statesFromJson(cached);
      haveCache_ = !states_.empty();
    }
    body_ = root.add<ui::Stack>();
    body_->flex = 1;
    std::vector<ui::TabBar::Tab> tabs;
    for (const Tab& t : config_.tabs) tabs.push_back({iconByName(t.icon, Icon::House), t.title});
    for (size_t i = 0; i < config_.tabs.size(); ++i) buildTab(static_cast<int>(i));
    status_ = root.add<ui::Label>(haveCache_ ? "Saved data" : "Loading...", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    tabbar_ = root.add<ui::TabBar>(tabs, 0);
    tabbar_->onSelect = [this](int i) {
      touched();
      showTab(i);
    };
    showTab(0);
    applyAll();
  }

  void onShow() override {
    touched();
    refresh(tab_, true);
  }
  void onHide() override { pollTimerArmed_ = false; }

  std::string debugState() const override {
    std::string s = "tab=" + std::to_string(tab_);
    for (const Tab& t : config_.tabs) s += "|" + t.title;
    s += "|" + statusText_;
    return s;
  }

 private:
  // ---- building

  struct Page {
    std::vector<const Item*> items;
    std::vector<ui::ListItem*> rows;
    std::vector<std::string> sigs;
    std::vector<const Item*> bound;  // the item each row serves (its callbacks are set only when this changes)
    ui::Pager* pager = nullptr;
    ui::Column* list = nullptr;
  };
  struct TabView {
    ui::Column* content = nullptr;
    Page page;                          // the paged rows (lights, switches, sensors)
    std::vector<ui::Button*> scenes;
    std::vector<ui::Tile*> tiles;       // rooms
    std::vector<const Item*> tileItems;
    ui::Column* climateCard = nullptr;  // thermostat
    const Item* climate = nullptr;
    ui::Label* temp = nullptr;
    ui::Label* mode = nullptr;
    ui::Stepper* target = nullptr;
    ui::Label* contacts = nullptr;
    std::vector<const Item*> contactItems;
  };

  ui::Column* buildTab(int index) {
    const Tab& tab = config_.tabs[index];
    TabView& v = views_.emplace_back();
    v.content = body_->add<ui::Column>(ui::s1);
    // Scenes: a row of buttons on top of the lights.
    std::vector<const Item*> scenes;
    for (const Item& it : tab.items) {
      if (it.type == "scene") scenes.push_back(&it);
      else if (it.type == "climate" && !v.climate) v.climate = &it;
      else if (it.type == "room") v.tileItems.push_back(&it);
      else if (it.type == "contact") v.contactItems.push_back(&it);
      else v.page.items.push_back(&it);
    }
    if (!scenes.empty()) {
      v.content->add<ui::Spacer>(ui::s1);
      auto* row = v.content->add<ui::Row>(ui::s1);
      for (const Item* sc : scenes) {
        auto* b = row->add<ui::Button>(sc->name, ui::Button::Kind::Secondary);
        b->flex = 1;
        b->onClick = [this, sc] {
          touched();
          runScene(*sc);
        };
        v.scenes.push_back(b);
      }
    }
    if (v.climate) buildClimateCard(v);
    if (!v.tileItems.empty()) {
      for (size_t i = 0; i < v.tileItems.size(); i += 2) {
        auto* row = v.content->add<ui::Row>(ui::s1, ui::Align::Start);
        for (size_t k = i; k < i + 2; ++k) {
          auto* t = row->add<ui::Tile>(Icon::Thermometer, k < v.tileItems.size() ? v.tileItems[k]->name : "");
          t->flex = 1;
          if (k < v.tileItems.size()) v.tiles.push_back(t);
          else t->setBlank(true);
        }
      }
    }
    if (!v.contactItems.empty()) {
      v.content->add<ui::Spacer>(ui::s1);
      v.contacts = v.content->add<ui::Label>("", Type::Body, ui::ink);
      v.contacts->maxLines = 3;
    }
    if (!v.page.items.empty()) buildPaged(v.page);
    return v.content;
  }

  void buildPaged(Page& p) {
    const int pages = std::max(1, (static_cast<int>(p.items.size()) + kRowsPerPage - 1) / kRowsPerPage);
    p.pager = views_.back().content->add<ui::Pager>(pages);
    p.pager->flex = 1;
    p.pager->bar()->visible = pages > 1;
    p.list = p.pager->add<ui::Column>(0);
    for (int i = 0; i < kRowsPerPage; ++i) {
      p.rows.push_back(p.list->add<ui::ListItem>());
      p.sigs.emplace_back();
      p.bound.push_back(nullptr);
    }
    Page* pp = &p;
    p.pager->onPage = [this, pp](int) {
      touched();
      fillPage(*pp);
    };
  }

  void buildClimateCard(TabView& v) {
    v.content->add<ui::Spacer>(ui::s1);
    v.climateCard = v.content->add<ui::Column>(ui::s1);
    auto* head = v.climateCard->add<ui::Row>(ui::s2);
    auto* name = head->add<ui::Label>(v.climate->name, Type::BodyStrong);
    name->flex = 1;
    v.mode = head->add<ui::Label>("", Type::Caption, ui::secondary);
    v.mode->align = ui::TextAlign::Right;
    v.temp = v.climateCard->add<ui::Label>("--", Type::Headline);
    // Half-degree steps: the stepper counts half degrees (10 = 5.0 C).
    v.target = v.climateCard->add<ui::Stepper>(40, 10, 60, 1, [](int h) { return "Target " + celsiusText(h / 2.0); });
    TabView* vp = &v;
    v.target->onChange = [this, vp](int halfDegrees) {
      touched();
      scheduleTarget(*vp->climate, halfDegrees / 2.0);
    };
    v.climateCard->add<ui::Spacer>(ui::s1);
  }

  // ---- showing

  void showTab(int i) {
    tab_ = i;
    for (size_t k = 0; k < views_.size(); ++k) views_[k].content->visible = static_cast<int>(k) == i;
    tabbar_->setSelected(i);
    applyAll();
    if (body_->host()) body_->host()->relayout();
    refresh(i, false);
  }

  std::string lightSubtitle(const LightView& v) const {
    if (v.unavailable) return "Unavailable";
    std::string s = v.on ? "On" : "Off";
    if (v.onCount >= 0 && v.groupSize > 1) s = std::to_string(v.onCount) + " of " + std::to_string(v.groupSize) + " on";
    if (v.on && v.percent >= 0) s += " - " + std::to_string(v.percent) + "%";
    return s;
  }

  // A row keeps its Toggle between refreshes (a tap on it is still running while the row is updated): only its look changes, and its
  // callbacks only when the row serves another item.
  void setRowToggle(ui::ListItem* row, bool on, const Item*& bound, const Item* it) {
    if (row->toggle() && bound == it) {
      row->toggle()->setOn(on, Hint::Content);
      return;
    }
    row->setToggle(on, [this, it](bool v) { onToggle(*it, v); });
    bound = it;
  }

  void configureRow(ui::ListItem* row, std::string& sig, const Item*& bound, const Item* it) {
    std::string next;
    if (!it) {
      row->visible = false;
      sig.clear();
      return;
    }
    row->visible = true;
    const States::const_iterator st = states_.find(it->entity);
    const bool known = st != states_.end() && st->second.known;
    if (it->type == "light") {
      const LightView lv = lightView(*it, states_);
      const std::string sub = known || lv.onCount >= 0 ? lightSubtitle(lv) : "";
      next = "L|" + it->name + "|" + sub + "|" + (lv.on ? "1" : "0");
      if (next == sig) return;
      sig = next;
      row->setIcon(iconByName(it->icon, lv.on ? Icon::Lightbulb : Icon::LightbulbOff));
      row->setTitle(it->name);
      row->setSubtitle(sub);
      setRowToggle(row, lv.on, bound, it);
      row->onClick = [this, it] {
        touched();
        openLightSheet(*it);
      };
    } else if (it->type == "switch" || it->type == "fan") {
      const bool on = known && st->second.on();
      next = "S|" + it->name + "|" + (known ? st->second.state : "?");
      if (next == sig) return;
      sig = next;
      row->setIcon(iconByName(it->icon, it->type == "fan" ? Icon::Fan : Icon::Power));
      row->setTitle(it->name);
      row->setSubtitle(known ? (st->second.unavailable() ? "Unavailable" : on ? "On" : "Off") : "");
      setRowToggle(row, on, bound, it);
      row->onClick = nullptr;
    } else {  // sensor
      std::string value = known ? st->second.state : "";
      if (known && !st->second.unit.empty()) value += " " + st->second.unit;
      next = "V|" + it->name + "|" + value;
      if (next == sig) return;
      sig = next;
      row->setIcon(Icon::Info);
      row->setTitle(it->name);
      row->setSubtitle("");
      row->setTrailingText(value);
      row->onClick = nullptr;
    }
  }

  void fillPage(Page& p) {
    const int page = p.pager ? p.pager->page() : 0;
    for (int i = 0; i < kRowsPerPage; ++i) {
      const size_t idx = static_cast<size_t>(page) * kRowsPerPage + i;
      configureRow(p.rows[i], p.sigs[i], p.bound[i], idx < p.items.size() ? p.items[idx] : nullptr);
    }
    if (p.list && p.list->frame.w > 0) p.list->layout();
  }

  void applyAll() {
    for (size_t i = 0; i < views_.size(); ++i) {
      TabView& v = views_[i];
      if (!v.page.items.empty()) fillPage(v.page);
      for (size_t k = 0; k < v.tiles.size(); ++k) {
        const Item* it = v.tileItems[k];
        auto t = states_.find(it->temperature);
        auto h = states_.find(it->humidity);
        std::string value;
        if (t != states_.end() && t->second.known) {
          value = t->second.unavailable() ? "n/a" : t->second.state + " " + (t->second.unit.empty() ? "\xC2\xB0" "C" : t->second.unit);
          if (h != states_.end() && h->second.known && !h->second.unavailable()) value += "  " + h->second.state + "%";
        }
        v.tiles[k]->setValue(value);
      }
      if (v.climate) {
        auto c = states_.find(v.climate->entity);
        if (c != states_.end() && c->second.known) {
          v.temp->setText(c->second.current > -999 ? celsiusText(c->second.current) : "--");
          v.mode->setText(c->second.state);
          if (c->second.target > -999 && !targetPending_) v.target->setValue(static_cast<int>(std::lround(c->second.target * 2)), Hint::Content);
        }
      }
      if (v.contacts) {
        std::string open;
        for (const Item* it : v.contactItems) {
          auto s = states_.find(it->entity);
          if (s != states_.end() && s->second.on()) open += (open.empty() ? "" : ", ") + it->name + " open";
        }
        bool any = false;
        for (const Item* it : v.contactItems) any = any || states_.count(it->entity);
        v.contacts->setText(any ? (open.empty() ? "Everything closed" : open) : "");
      }
    }
  }

  void setStatus(const std::string& t) {
    statusText_ = t;
    status_->setText(t);
  }

  // ---- reading

  void touched() {
    lastTouchMs_ = ctx().clock.millis();
    armPoll();
  }

  void armPoll() {
    if (pollTimerArmed_) return;
    pollTimerArmed_ = true;
    ctx().timer(kPollMs, [this] {
      pollTimerArmed_ = false;
      if (ctx().clock.millis() - lastTouchMs_ < kActiveWindowMs) {  // used lately: keep the visible tab fresh
        refresh(tab_, true);
        armPoll();
      }
    });
  }

  void refresh(int tab, bool force) {
    if (busy_) {
      pendingTab_ = tab;
      return;
    }
    (void)force;
    std::vector<std::string> ids = entitiesOf(config_.tabs[tab]);
    if (ids.empty()) return;
    busy_ = true;
    if (!haveCache_ || statusText_ == "Loading...") setStatus("Loading...");
    std::vector<std::string> paths;
    for (const std::string& id : ids) paths.push_back("/api/states/" + id);
    HaClient ha = ctx().ha();
    ctx().async([ha, paths]() mutable { return ha.getMany(paths); },
                [this, tab, ids](std::vector<HaResult> results) {
                  busy_ = false;
                  int ok = 0, notFound = 0;
                  HaError firstError = HaError::None;
                  for (size_t i = 0; i < results.size(); ++i) {
                    EntityState s;
                    if (results[i].ok() && parseState(results[i].body, s)) {
                      states_[ids[i]] = s;
                      ++ok;
                    } else {
                      if (results[i].error == HaError::NotFound) ++notFound;
                      if (firstError == HaError::None) firstError = results[i].error == HaError::None ? HaError::Other : results[i].error;
                    }
                  }
                  if (ok > 0) {
                    haveCache_ = true;
                    fs::writeFile(kCacheFile, statesToJson(states_));
                    applyAll();
                    if (body_->host()) body_->host()->relayout();
                  }
                  if (ok == static_cast<int>(results.size())) {
                    setStatus("Updated " + clockText());
                    ctx().log("tab %d read: %d entities", tab, ok);
                  } else if (ok > 0) {
                    setStatus(std::to_string(notFound) + " not found in Home Assistant");
                  } else {
                    setStatus(std::string("Saved data: ") + haErrorText(firstError));
                  }
                  if (pendingTab_ >= 0) {
                    const int t = pendingTab_;
                    pendingTab_ = -1;
                    refresh(t, true);
                  }
                });
  }

  std::string clockText() {
    const int64_t t = ctx().clock.now();
    return t > 0 ? pos::clockText(t) : "just now";
  }

  // ---- acting

  // Runs service calls on the worker; `revert` runs on the UI thread if any failed.
  void send(std::vector<Call> calls, std::function<void()> revert, const std::string& okToast = "") {
    HaClient ha = ctx().ha();
    ctx().async(
        [ha, calls]() mutable {
          HaError err = HaError::None;
          for (const Call& c : calls) {
            std::string domain, name;
            if (!splitService(c.service, domain, name)) continue;
            HaResult r = ha.callService(domain, name, callBody(c));
            if (!r.ok() && err == HaError::None) err = r.error;
          }
          return static_cast<int>(err);
        },
        [this, revert, okToast](int err) {
          if (err != static_cast<int>(HaError::None)) {
            if (revert) revert();
            ctx().toast(haErrorText(static_cast<HaError>(err)));
            return;
          }
          if (!okToast.empty()) ctx().toast(okToast);
          ctx().timer(kRefreshAfterActionMs, [this] { refresh(tab_, true); });  // lights report their new state with a delay
        });
  }

  void setLocal(const std::string& entity, const std::string& state, int brightness = -2) {
    EntityState& s = states_[entity];
    s.known = true;
    s.state = state;
    if (brightness != -2) s.brightness = brightness;
    // A group's stand-in and its members move together: the read-only group follows, so the "x of y" count stays right.
  }

  void onToggle(const Item& it, bool on) {
    touched();
    // The stand-in, its read-only group and the members all follow at once; a failure puts every one of them back.
    std::vector<std::string> touched_ids = {it.entity};
    if (!it.group.empty()) touched_ids.push_back(it.group);
    for (const std::string& m : it.members) touched_ids.push_back(m);
    for (const std::string& m : it.also) touched_ids.push_back(m);
    auto saved = std::make_shared<std::vector<std::pair<std::string, EntityState>>>();
    for (const std::string& id : touched_ids) {
      saved->emplace_back(id, states_.count(id) ? states_[id] : EntityState{});
      setLocal(id, on ? "on" : "off");
    }
    applyAll();
    const std::string domain = it.type == "light" ? "light" : it.type == "fan" ? "fan" : "switch";
    std::vector<Call> calls = {Call{domain + (on ? ".turn_on" : ".turn_off"), it.entity, "{}"}};
    for (const std::string& e : it.also) calls.push_back(Call{domain + (on ? ".turn_on" : ".turn_off"), e, "{}"});  // on: each at its own level
    send(calls, [this, saved] {
      for (const auto& kv : *saved) states_[kv.first] = kv.second;
      applyAll();
    });
  }

  void onBrightness(const Item& it, int percent) {
    touched();
    const EntityState before = states_.count(it.entity) ? states_[it.entity] : EntityState{};
    setLocal(it.entity, "on", percent * 255 / 100);
    applyAll();
    const std::string entity = it.entity;
    send({Call{"light.turn_on", entity, "{\"brightness_pct\":" + std::to_string(percent) + "}"}},
         [this, entity, before] {
           states_[entity] = before;
           applyAll();
         });
  }

  void runScene(const Item& scene) {
    ctx().toast(scene.name + "...");
    send(scene.calls, nullptr, scene.name + " set");
  }

  void scheduleTarget(const Item& climate, double target) {
    targetPending_ = true;
    if (targetTimer_) ctx().os().timers().cancel(targetTimer_);
    const std::string entity = climate.entity;
    targetTimer_ = ctx().timer(800, [this, entity, target] {
      targetTimer_ = 0;
      targetPending_ = false;
      char data[48];
      std::snprintf(data, sizeof data, "{\"temperature\":%.1f}", target);
      const EntityState before = states_.count(entity) ? states_[entity] : EntityState{};
      states_[entity].target = target;
      send({Call{"climate.set_temperature", entity, data}}, [this, entity, before] {
        states_[entity] = before;
        applyAll();
      });
    });
  }

  void openLightSheet(const Item& it) {
    ui::openSheet(*body_, [&](ui::Sheet& sheet) {
      ui::Column* c = sheet.content();
      const LightView lv = lightView(it, states_);
      c->add<ui::Label>(it.name, Type::Title);
      if (it.dimmable) {
        // "Brightness" on the left, the value on the right ("60%", "--" when unknown), the ten boxes below.
        auto* head = c->add<ui::Row>(ui::s1);
        auto* cap = head->add<ui::Label>("Brightness", Type::Caption, ui::secondary);
        cap->flex = 1;
        const int shown = lv.percent >= 0 ? (lv.percent + 5) / 10 * 10 : -1;
        auto* value = head->add<ui::Label>(shown >= 0 ? std::to_string(shown) + "%" : "--", Type::BodyStrong);
        value->align = ui::TextAlign::Right;
        auto* level = c->add<ui::LevelBar>(shown >= 0 ? shown : 50);
        const Item* ip = &it;
        level->onChange = [this, ip, value](int pct) {
          value->setText(std::to_string(pct) + "%");
          onBrightness(*ip, pct);
        };
      }
      if (!it.members.empty()) {
        c->add<ui::Label>("Lamps", Type::Caption, ui::secondary);
        for (const std::string& m : it.members) {
          auto* row = c->add<ui::ListItem>(nameOf(config_, m));
          auto s = states_.find(m);
          const std::string member = m;
          row->setToggle(s != states_.end() && s->second.on(), [this, member](bool on) {
            touched();
            const EntityState before = states_.count(member) ? states_[member] : EntityState{};
            setLocal(member, on ? "on" : "off");
            send({Call{on ? "light.turn_on" : "light.turn_off", member, "{}"}}, [this, member, before] {
              states_[member] = before;  // as the tiles do: back to what it was, and shown
              applyAll();
            });
          });
        }
      }
      sheet.addButton("Done", {}, ui::Button::Kind::Secondary);
      sheet.onClose = [this] { applyAll(); };
    });
  }

  Config config_;
  States states_;
  bool haveCache_ = false;
  std::deque<TabView> views_;
  ui::Stack* body_ = nullptr;
  ui::Label* status_ = nullptr;
  ui::TabBar* tabbar_ = nullptr;
  int tab_ = 0;
  bool busy_ = false;
  int pendingTab_ = -1;
  std::string statusText_;
  uint32_t lastTouchMs_ = 0;
  bool pollTimerArmed_ = false;
  bool targetPending_ = false;
  Timers::Id targetTimer_ = 0;
};

class HomeApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<HomeScreen>(); }
};

POS_REGISTER_APP(HomeApp, {.id = "home", .name = "Home", .icon = Icon::House, .order = 20});

}  // namespace

}  // namespace pos::home
