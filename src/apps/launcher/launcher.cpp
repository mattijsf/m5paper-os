#include "apps/launcher/launcher.h"

#include <cstdlib>
#include <ctime>

#include "core/fs.h"
#include "core/store.h"
#include "core/timefmt.h"
#include "ui/widgets.h"

namespace pos::apps::launcher {

namespace {

constexpr int kPerPage = 6;  // 2 columns x 3 rows

std::string dateLine(hal::Clock& clock) {
  const int64_t now = clock.now();
  if (now <= 0) return "PaperOS";  // the RTC is not set yet
  static const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char* kMonths[] = {"January", "February", "March", "April", "May", "June",
                                  "July", "August", "September", "October", "November", "December"};
  struct tm tmv;
  localTime(now, tmv);  // net.timezone
  return std::string(kDays[tmv.tm_wday]) + " " + std::to_string(tmv.tm_mday) + " " + kMonths[tmv.tm_mon];
}

// What the summary line is made of: the badges the apps cached (never the network) and the reader's last book.
std::string computeSummary(const std::vector<const AppEntry*>& apps) {
  int unread = 0;
  for (const AppEntry* a : apps) {
    std::string raw;
    fs::readFile(std::string("/data/cache/") + a->info.id + "/badge", raw);
    unread += std::max(0, std::atoi(raw.c_str()));
  }
  Store reader("/data/apps/read.json");
  return summaryLine(unread, reader.get("last.title", std::string()), reader.has("last.percent") ? reader.get("last.percent", -1) : -1);
}

// Home screen: header, then a 2 x 3 grid of tiles from the app registry (sorted by order), paged if
// there are more than six apps. A tap opens the app.
class LauncherScreen : public Screen {
 public:
  explicit LauncherScreen(Shell& shell) : shell_(shell), apps_(AppRegistry::instance().sorted()) {}

  void build(ui::Column& root) override {
    root.add<ui::Spacer>(ui::s3);
    date_ = root.add<ui::Label>(dateLine(shell_.os().platform().clock), ui::Type::Title);
    summary_ = root.add<ui::Label>(computeSummary(apps_), ui::Type::Caption, ui::secondary);
    root.add<ui::Spacer>(ui::s3);

    const int pages = std::max(1, (static_cast<int>(apps_.size()) + kPerPage - 1) / kPerPage);
    auto* pager = root.add<ui::Pager>(pages);
    pager->flex = 1;
    pager->bar()->visible = pages > 1;
    auto* grid = pager->add<ui::Column>(ui::s3);
    for (int r = 0; r < 3; ++r) {
      auto* row = grid->add<ui::Row>(ui::s3, ui::Align::Start);
      for (int c = 0; c < 2; ++c) {
        auto* tile = row->add<ui::Tile>();
        tile->flex = 1;
        tiles_.push_back(tile);
      }
    }
    pager->onPage = [this](int page) { fill(page); };
    fill(0);
  }

  void onShow() override {
    summary_->setText(computeSummary(apps_));  // the layout that follows makes room for it
    // Badges come from what apps cached earlier; the launcher never does network I/O.
    for (size_t i = 0; i < tiles_.size(); ++i) {
      const int idx = page_ * kPerPage + static_cast<int>(i);
      if (idx >= static_cast<int>(apps_.size())) continue;
      std::string raw;
      fs::readFile(std::string("/data/cache/") + apps_[idx]->info.id + "/badge", raw);
      tiles_[i]->setBadge(std::atoi(raw.c_str()));
    }
  }

 private:
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
      t->setBadge(0);
      const std::string id = info.id;
      t->onClick = [this, id] { shell_.requestOpen(id); };
    }
  }

  Shell& shell_;
  std::vector<const AppEntry*> apps_;
  ui::Label *date_ = nullptr, *summary_ = nullptr;
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
