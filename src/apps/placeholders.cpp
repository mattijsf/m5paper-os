// Placeholder apps so the launcher grid is real before the apps exist: each shows "Coming soon".
// (Settings is real since P2-6.) Delete an entry here when its real app is registered (Read: done in P4, Home: P5, WhatsApp/Mail: P6).
#include "core/app.h"
#include "ui/widgets.h"

namespace pos::apps::placeholders {

namespace {

class ComingSoonScreen : public Screen {
 public:
  ComingSoonScreen(std::string name, Icon icon) : name_(std::move(name)), icon_(icon) {}
  void build(ui::Column& root) override {
    setTitle(name_);
    auto* es = root.add<ui::EmptyState>(icon_, "Coming soon", "This app arrives in a later update.");
    es->flex = 1;
  }

 private:
  std::string name_;
  Icon icon_;
};


class ComingSoonApp : public App {
 public:
  ComingSoonApp(const char* name, Icon icon) : name_(name), icon_(icon) {}
  void onStart(AppContext& ctx) override { ctx.nav.push<ComingSoonScreen>(name_, icon_); }

 private:
  std::string name_;
  Icon icon_;
};



}  // namespace

}  // namespace pos::apps::placeholders
