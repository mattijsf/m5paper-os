// Agenda: the paired phone's calendar for today and the next days (docs/apps/agenda.md, DECISIONS.md D28). One way: the
// phone sends its calendar through the encrypted relay in Home Assistant (docs/COMPANION.md 7.1 `agenda`), this app shows
// what the Paper holds (core/agenda) and reads the relay when it opens. Nothing is changed on the phone. Two views on a tab
// bar: Day (time blocks on an hour grid, apps/agenda/day_view) and List (every event of the next two weeks, paged).
#include <algorithm>
#include <climits>
#include <iterator>
#include <memory>

#include "apps/agenda/day_view.h"
#include "apps/common/ha_error_state.h"
#include "apps/inbox/inbox_model.h"
#include "apps/inbox/relay_fetch.h"
#include "apps/inbox/scheduled_refresh.h"
#include "core/agenda.h"
#include "core/app.h"
#include "core/companion.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::agenda {

namespace {

using ui::Type;

constexpr int kSlots = 6;          // rows on a page at most
constexpr int kRowH = 96;          // ui::ListItem
constexpr int kHeaderH = 52;       // a day's name above its first row on a page (space + one BodyStrong line)
constexpr int kPageBudget = 620;   // rows and headers of one page fit in this height (above the pager and tab bars)
constexpr uint32_t kStaleMs = 60 * 1000;

// One row of the list: an event, or the "N more" line that closes a day the phone had to cut.
struct Entry {
  int date = 0;
  int event = -1;  // index into the day's events; -1: the "more" line
  int more = 0;
  bool header = false;  // the day's name is shown above this row (first row of the day on its page)
};

struct Fetched {
  inbox::RelayFetch relay;
  Agenda agenda;
};

class AgendaScreen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Agenda");
    setAction(Icon::RefreshCw, [this] { refresh(); });
    holder_ = root.add<ui::Column>(0);
    holder_->flex = 1;
    status_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    view_ = ctx().store.get("view", 0) == 1 ? 1 : 0;
    tabs_ = root.add<ui::TabBar>(std::vector<ui::TabBar::Tab>{{Icon::Calendar, "Day"}, {Icon::List, "List"}}, view_);
    tabs_->onSelect = [this](int i) {
      if (i == view_) return;
      view_ = i;
      ctx().store.set("view", view_);
      rebuild();
    };
    notPaired_ = !companion::pairedPhone();
    if (!notPaired_) load(agenda_);
    rebuild();
    armMinute();
  }

  void onShow() override {
    if (!fetchedOnce_ || ctx().clock.millis() - lastFetchMs_ > kStaleMs) refresh();
  }

  std::string debugState() const override {
    return std::string(view_ == 0 ? "view=day|date=" + formatDate(day_ ? day_->date() : dayDate_) : "view=list") +
           "|rows=" + std::to_string(entries_.size()) + "|pages=" + std::to_string(pages_.size()) + "|" + status_->text() +
           (error_ ? "|error" : "") + (notSetUp_ ? "|notsetup" : "") + (notPaired_ ? "|notpaired" : "") +
           (!notPaired_ && !agenda_.received ? "|nothing-received" : "") + "|unchanged-reads=" + std::to_string(skipped_);
  }

 private:
  void empty(Icon icon, const std::string& title, const std::string& hint) {
    tabs_->visible = false;
    holder_->add<ui::EmptyState>(icon, title, hint)->flex = 1;
    holder_->host()->relayout();
  }

  // The rows of the days from today on, cut into pages by height.
  void paginate() {
    entries_.clear();
    pages_.clear();
    clockSet_ = localNow(ctx().clock.now(), today_, minute_);
    if (!clockSet_) {  // no clock: every stored day, by its date
      today_ = INT_MIN / 2;
      minute_ = -1;
    }
    int used = 0, rows = 0, lastDate = INT_MIN;
    for (const Day& day : agenda_.days) {
      if (day.date < today_) continue;
      const int n = static_cast<int>(day.events.size()) + (day.more > 0 ? 1 : 0);
      for (int i = 0; i < n; ++i) {
        Entry e;
        e.date = day.date;
        e.event = i < static_cast<int>(day.events.size()) ? i : -1;
        e.more = day.more;
        const bool newPage = pages_.empty() || rows >= kSlots || used + kRowH + (day.date != lastDate ? kHeaderH : 0) > kPageBudget;
        if (newPage) {
          pages_.push_back(static_cast<int>(entries_.size()));
          used = 0;
          rows = 0;
          lastDate = INT_MIN;
        }
        e.header = day.date != lastDate;
        used += kRowH + (e.header ? kHeaderH : 0);
        ++rows;
        lastDate = day.date;
        entries_.push_back(e);
      }
    }
  }

  const Day* dayOf(int date) const {
    for (const Day& d : agenda_.days)
      if (d.date == date) return &d;
    return nullptr;
  }

  void rebuild() {
    rebuilt_ = true;
    holder_->removeAll();
    headers_.clear();
    rows_.clear();
    gaps_.clear();
    pager_ = nullptr;
    day_ = nullptr;
    paginate();
    if (notPaired_) {
      entries_.clear();
      pages_.clear();
      return empty(Icon::Calendar, "No phone paired",
                   "The agenda comes from the PaperOS app on your phone. Pair it in Settings > Home Assistant > Phone.");
    }
    if (notSetUp_) return empty(Icon::Calendar, "Not set up yet", "The relay for your phone is missing in Home Assistant.");
    if (error_ && !agenda_.received) {
      tabs_->visible = false;
      apps::addHaErrorState(*holder_, failure_, [this] { refresh(); });
      holder_->host()->relayout();
      return;
    }
    if (!agenda_.received) return empty(Icon::Calendar, "No agenda yet", "Switch on \"Share the agenda\" in the PaperOS app on your phone.");
    tabs_->visible = true;
    if (view_ == 0) return buildDay();
    if (entries_.empty()) {
      empty(Icon::Calendar, "Nothing planned", "The next two weeks are free.");
      tabs_->visible = true;
      return;
    }
    const int pages = static_cast<int>(pages_.size());
    pager_ = holder_->add<ui::Pager>(pages);
    pager_->flex = 1;
    pager_->bar()->visible = pages > 1;
    auto* list = pager_->add<ui::Column>(0);
    for (int i = 0; i < kSlots; ++i) {
      gaps_.push_back(list->add<ui::Spacer>(ui::s2));
      headers_.push_back(list->add<ui::Label>("", Type::BodyStrong));
      rows_.push_back(list->add<ui::ListItem>());
    }
    pager_->onPage = [this](int p) { fill(p); };
    fill(0);
    holder_->host()->relayout();
  }

  void fill(int page) {
    const int first = pages_[page];
    const int end = page + 1 < static_cast<int>(pages_.size()) ? pages_[page + 1] : static_cast<int>(entries_.size());
    for (int i = 0; i < kSlots; ++i) {
      const int idx = first + i;
      const bool used = idx < end;
      rows_[i]->visible = used;
      headers_[i]->visible = gaps_[i]->visible = used && entries_[idx].header;
      if (!used) continue;
      const Entry& e = entries_[idx];
      if (e.header) headers_[i]->setText(dayTitle(e.date, today_));
      ui::ListItem* row = rows_[i];
      const Day* day = dayOf(e.date);
      if (e.event < 0 || !day) {
        row->setTitle(std::to_string(e.more) + (e.more == 1 ? " more event" : " more events"));
        row->setSubtitle("On your phone");
        row->setSubtitleIsContent(false);
        row->onClick = nullptr;
        continue;
      }
      const Event& ev = day->events[e.event];
      row->setTitle(ev.title.empty() ? "(No title)" : ev.title);
      row->setSubtitle(timeText(ev) + (ev.location.empty() ? "" : "  \xC2\xB7  " + ev.location));
      row->setSubtitleIsContent(true);  // the time is what the list is for
      row->onClick = [this, e] { open(e.date, e.event); };
    }
    if (!rows_.empty()) {
      auto* parent = rows_[0]->parent();
      if (parent && parent->frame.w > 0) parent->layout();
    }
  }

  // The day view: today to the end of the two weeks the phone sends (without a clock: the days the Paper holds). The shown
  // day survives a refresh.
  void buildDay() {
    int first = today_, last = today_ + kMaxRunDays - 1;
    if (!clockSet_) {
      first = agenda_.days.empty() ? 0 : agenda_.days.front().date;
      last = agenda_.days.empty() ? 0 : agenda_.days.back().date;
    }
    if (dayDate_ < first || dayDate_ > last) dayDate_ = first;
    day_ = holder_->add<DayView>(agenda_, today_, clockSet_ ? minute_ : -1, first, last);
    day_->setDate(dayDate_);
    day_->onDay = [this](int d) { dayDate_ = d; };
    day_->onOpen = [this](int date, int event) { open(date, event); };
    holder_->host()->relayout();
  }

  // The whole event: a long title and location do not fit a row.
  void open(int date, int event) {
    const Day* day = dayOf(date);
    if (!day || event < 0 || event >= static_cast<int>(day->events.size())) return;
    const Event& ev = day->events[event];
    ui::openSheet(*holder_, [&](ui::Sheet& sheet) {
      ui::Column* c = sheet.content();
      c->add<ui::Label>(ev.title.empty() ? "(No title)" : ev.title, Type::Title)->maxLines = 3;
      c->add<ui::Label>(dayTitle(date, today_) + ", " + timeText(ev), Type::Body);
      if (!ev.location.empty()) c->add<ui::Label>(ev.location, Type::Body, ui::secondary)->maxLines = 2;
      sheet.addButton("Close", {}, ui::Button::Kind::Secondary);
    });
  }

  // Once a minute (like the status bar clock): the now line moves, and at midnight the views start from the new today.
  void armMinute() {
    const int64_t now = ctx().clock.now();
    const uint32_t ms = now > 0 ? static_cast<uint32_t>(60 - now % 60) * 1000 : 60000;
    ctx().timer(ms, [this] {
      tickNow();
      armMinute();
    });
  }

  void tickNow() {
    int today = 0, minute = -1;
    const bool set = localNow(ctx().clock.now(), today, minute);
    if (set != clockSet_ || (set && today != today_)) {
      if (rebuilt_) rebuild();
      return;
    }
    minute_ = set ? minute : -1;
    if (day_) day_->setNow(minute_);
  }

  void showStatus() {
    if (!agenda_.received) return status_->setText("");
    const std::string when = inbox::relativeTime(agenda_.updated, ctx().clock.now());
    status_->setText(when.empty() ? "From your phone" : "From your phone: " + when);
  }

  void refresh() {
    if (busy_) return;
    notPaired_ = !companion::pairedPhone();
    if (notPaired_) {
      agenda_ = Agenda{};
      status_->setText("");
      rebuild();
      return;
    }
    busy_ = true;
    if (!agenda_.received) status_->setText("Loading...");
    HaClient ha = ctx().ha();
    const int64_t now = ctx().clock.now();
    const bool clip = ctx().config.phone.shareClipboard;
    ctx().async(
        [ha, now, clip]() mutable {
          Fetched f;
          f.relay = inbox::fetchRelay(ha, now, clip);
          load(f.agenda);
          return f;
        },
        [this](Fetched f) {
          busy_ = false;
          fetchedOnce_ = true;
          lastFetchMs_ = ctx().clock.millis();
          // What the view showed before this read: when nothing it depends on changed, only the status line is redrawn
          // (a rebuild lays out and redraws the whole screen, ~0.4 MB of canvas, to push nothing).
          const bool before[] = {notPaired_, notSetUp_, error_ && !agenda_.received};
          const Agenda shown = agenda_;
          notPaired_ = !f.relay.paired;
          notSetUp_ = f.relay.http.error == HaError::NotFound;
          error_ = !notPaired_ && !notSetUp_ && !(f.relay.http.ok() && f.relay.applied.parsed);
          failure_ = !error_ ? HaError::None : f.relay.http.ok() ? HaError::Other : f.relay.http.error;
          agenda_ = notPaired_ ? Agenda{} : f.agenda;
          if (notPaired_ && f.relay.applied.unpaired) ctx().toast("Your phone ended the pairing");
          if (error_ && agenda_.received) ctx().toast(f.relay.http.ok() ? "Unexpected answer from Home Assistant" : haErrorText(f.relay.http.error));
          if (notPaired_ || notSetUp_) status_->setText("");
          else if (error_) status_->setText(agenda_.received ? "Saved data" : "");
          else showStatus();
          const bool after[] = {notPaired_, notSetUp_, error_ && !agenda_.received};
          if (!rebuilt_ || !agenda_.sameContent(shown) || !std::equal(std::begin(before), std::end(before), std::begin(after))) rebuild();
          else {
            ++skipped_;
            tickNow();
          }
          if (!error_ && f.relay.clipFromPhone) ctx().toast("Clipboard from your phone");
        });
  }

  Agenda agenda_;
  std::vector<Entry> entries_;
  std::vector<int> pages_;  // index of each page's first entry
  int today_ = 0, minute_ = -1, dayDate_ = INT_MIN / 2;
  int view_ = 0;  // 0 day, 1 list (remembered in the app's store)
  bool clockSet_ = false;
  bool rebuilt_ = false;  // set by rebuild(): the views exist
  int skipped_ = 0;       // reads that changed nothing on screen (debugState: UI tests check the skip)
  bool busy_ = false, error_ = false, notSetUp_ = false, notPaired_ = false, fetchedOnce_ = false;
  HaError failure_ = HaError::None;  // why the last read failed (while error_)
  uint32_t lastFetchMs_ = 0;
  ui::Column* holder_ = nullptr;
  ui::Pager* pager_ = nullptr;
  ui::Label* status_ = nullptr;
  ui::TabBar* tabs_ = nullptr;
  DayView* day_ = nullptr;
  std::vector<ui::Spacer*> gaps_;
  std::vector<ui::Label*> headers_;
  std::vector<ui::ListItem*> rows_;
};

class AgendaApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<AgendaScreen>(); }
};

POS_REGISTER_APP(AgendaApp, {
    .id = "agenda",
    .name = "Agenda",
    .icon = Icon::Calendar,
    .order = 97,  // after the games: the tiles of the other apps keep their places
});

}  // namespace

}  // namespace pos::agenda
