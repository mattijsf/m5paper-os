#include "apps/launcher/launcher.h"

#include "ui/widgets.h"

namespace pos::apps::launcher {

namespace {

// TEMPORARY (P2-4): a plain list of the registered apps. P2-5 replaces it with the tile grid.
class LauncherScreen : public Screen {
 public:
  explicit LauncherScreen(Shell& shell) : shell_(shell) {}
  void build(ui::Column& root) override {
    setTitle("PaperOS");
    root.add<ui::Spacer>(ui::s2);
    for (const AppEntry* e : AppRegistry::instance().sorted()) {
      auto* item = root.add<ui::ListItem>(e->info.name, "", true, e->info.icon);
      item->setChevron();
      const std::string id = e->info.id;
      item->onClick = [this, id] { shell_.requestOpen(id); };
    }
  }

 private:
  Shell& shell_;
};

}  // namespace

void install(Shell& shell) {
  shell.setLauncher([](Shell& s) { return std::make_unique<LauncherScreen>(s); });
}

}  // namespace pos::apps::launcher
