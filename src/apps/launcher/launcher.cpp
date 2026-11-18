#include "apps/launcher/launcher.h"

#include <memory>

#include <cstdlib>
#include <ctime>

#include "apps/inbox/relay_fetch.h"
#include "core/agenda.h"
#include "core/fs.h"
#include "core/log.h"
#include "core/sleep_screen.h"
#include "core/store.h"
#include "core/timefmt.h"
#include "ui/widgets.h"

namespace pos::apps::launcher {

namespace {

// Three columns of four small tiles: twelve apps to a page (the nine there are fit one page; until 2026-11-17 it was
// 2 x 3 large tiles, and the seventh app was on a second page). Four rows of 160 with 16 between end where the page bar
// starts, also with the agenda line above them.
constexpr int kColumns = 3, kRows = 4;
constexpr int kPerPage = kColumns * kRows;
// Between the header lines and the grid. The header row is as high as its power button (72), 12 more than the 24 + 36 the
// date alone took: this much less than s3 here keeps the grid where it was (four rows end where the page bar starts).
constexpr int kGridGap = ui::s3 - 12;

std::string dateLine(hal::Clock& clock) {
  const int64_t now = clock.now();
  if (now <= 0) return "PaperOS";  // the RTC is not set yet
  struct tm tmv;
  localTime(now, tmv);  // net.timezone
  return std::string(weekdayName(tmv.tm_wday)) + " " + std::to_string(tmv.tm_mday) + " " + monthName(tmv.tm_mon);
}

// What the summary line is made of: the badges the apps cached (never the network) and the reader's last book.
std::string computeSummary(const std::vector<const AppEntry*>& apps) {
  (void)apps;
  const int unread = cachedUnread();  // messages only: the Read tile's own badge counts articles
  Store reader("/data/apps/read.json");
  return summaryLine(unread, reader.get("last.title", std::string()), reader.has("last.percent") ? reader.get("last.percent", -1) : -1);
}

// What is on now or comes next in the phone's agenda (core/agenda: the saved days, never the network). "" for none.
std::string agendaLine(hal::Clock& clock) {
  agenda::Agenda a;
  if (!agenda::load(a)) return "";
  return agenda::nextLine(a, clock.now());
}

// Home screen: header, then a 2 x 3 grid of tiles from the app registry (sorted by order), paged if
// there are more than six apps. A tap opens the app.
class LauncherScreen : public Screen {
 public:
  explicit LauncherScreen(Shell& shell) : shell_(shell), apps_(AppRegistry::instance().sorted()) {}

  void build(ui::Column& root) override {
    // The date, and the power button in the corner: a tap switches the device
    // off through the sleep screen, which says so. The side button cannot: the firmware does not see it (HARDWARE.md), and
    // its double click cuts the power with whatever was on the panel still there.
    auto* head = root.add<ui::Row>(ui::s2);
    date_ = head->add<ui::Label>(dateLine(shell_.os().platform().clock), ui::Type::Title);
    date_->flex = 1;
    date_->maxLines = 1;
    auto* power = head->add<ui::IconButton>(ui::Icon::Power);
    power->onClick = [this] { shell_.sleepNow(); };
    agenda_ = root.add<ui::Label>("", ui::Type::Body);  // content (the next event): ink, body size, one line
    agenda_->maxLines = 1;
    showAgenda();
    summary_ = root.add<ui::Label>(computeSummary(apps_), ui::Type::Body);  // content (unread, the book being read): ink, body size
    root.add<ui::Spacer>(kGridGap);

    const int pages = std::max(1, (static_cast<int>(apps_.size()) + kPerPage - 1) / kPerPage);
    auto* pager = root.add<ui::Pager>(pages);
    pager->flex = 1;
    pager->bar()->visible = pages > 1;
    auto* grid = pager->add<ui::Column>(ui::s2);
    for (int r = 0; r < kRows; ++r) {
      auto* row = grid->add<ui::Row>(ui::s2, ui::Align::Start);
      for (int c = 0; c < kColumns; ++c) {
        auto* tile = row->add<ui::Tile>(ui::Icon::House, "", ui::Tile::Size::Small);
        tile->flex = 1;
        tiles_.push_back(tile);
      }
    }
    pager->onPage = [this](int page) { fill(page); };
    fill(0);
  }

  void onShow() override {
    hal::Clock& clock = shell_.os().platform().clock;
    const uint32_t t0 = clock.millis();
    showAgenda();
    const uint32_t t1 = clock.millis();
    summary_->setText(computeSummary(apps_));  // the layout that follows makes room for it
    const uint32_t t2 = clock.millis();
    showBadges();
    const uint32_t t3 = clock.millis();
    if (t3 - t0 >= 20)
      POS_LOG("perf", "launcher shown in %ums: agenda %u, summary %u, badges %u", static_cast<unsigned>(t3 - t0), static_cast<unsigned>(t1 - t0),
              static_cast<unsigned>(t2 - t1), static_cast<unsigned>(t3 - t2));
    checkPhoneSoon();
  }

 private:
  // Fresh badges without opening an app: when the launcher comes up and the phone's relay was not read for ten minutes,
  // read it in the background (inbox::checkRelay; Wi-Fi comes up for a few seconds). A moment after the launcher shows,
  // so its own first frame and a quick tap into an app go first; what changed is then set on the tiles and lines that
  // are there (a tile whose count did not change is not drawn again).
  void checkPhoneSoon() {
    if (!inbox::relayCheckDue()) return;
    std::weak_ptr<bool> alive = alive_;
    shell_.os().timers().after(kCheckDelayMs, [this, alive] {
      if (alive.expired() || shell_.appOpen() || !inbox::relayCheckDue()) return;
      inbox::checkRelay(shell_.os(), [this, alive] {
        if (alive.expired() || shell_.appOpen()) return;  // (an app on top: the launcher shows the new counts when it is back)
        const bool agendaWas = agenda_->visible;
        showAgenda();
        summary_->setText(computeSummary(apps_));
        showBadges();
        if (agenda_->visible != agendaWas && summary_->host()) summary_->host()->relayout();  // the line came or went
      });
    });
  }

  // The agenda line takes no room when there is nothing to say.
  void showAgenda() {
    const std::string line = agendaLine(shell_.os().platform().clock);
    agenda_->visible = !line.empty();
    agenda_->setText(line);
  }

  // Badges come from what apps cached earlier; the launcher never does network I/O.
  void showBadges() {
    for (size_t i = 0; i < tiles_.size(); ++i) {
      const int idx = page_ * kPerPage + static_cast<int>(i);
      if (idx < static_cast<int>(apps_.size())) tiles_[i]->setBadge(appBadge(apps_[idx]->info.id));
    }
  }

  void fill(int page) {
    page_ = page;
    for (size_t i = 0; i < tiles_.size(); ++i) {
      const int idx = page * kPerPage + static_cast<int>(i);
      ui::Tile* t = tiles_[i];
      if (idx >= static_cast<int>(apps_.size())) {
        t->setBlank(true);
        continue;
      }
      const AppInfo& info = apps_[idx]->info;
      t->setBlank(false);
      t->setIcon(info.icon);
      t->setLabel(info.name);
      const std::string id = info.id;
      t->onClick = [this, id] { shell_.requestOpen(id); };
    }
    showBadges();  // of the page now on screen (they showed 0 after a page turn until the launcher was shown again)
  }

  static constexpr uint32_t kCheckDelayMs = 1500;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  Shell& shell_;
  std::vector<const AppEntry*> apps_;
  ui::Label *date_ = nullptr, *agenda_ = nullptr, *summary_ = nullptr;
  std::vector<ui::Tile*> tiles_;
  int page_ = 0;
};

}  // namespace

std::string summaryLine(int unread, const std::string& title, int percent) {
  std::string s;
  if (unread > 0) s = std::to_string(unread) + " unread";
  if (!title.empty()) {
    if (!s.empty()) s += " - ";
    s += "Reading: " + title;
    if (percent >= 0) s += ", " + std::to_string(std::min(100, percent)) + "%";
  }
  return s;
}

void install(Shell& shell) {
  shell.setLauncher([](Shell& s) { return std::make_unique<LauncherScreen>(s); });
}

}  // namespace pos::apps::launcher
