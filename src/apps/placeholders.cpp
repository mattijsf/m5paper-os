// Placeholder apps so the launcher grid is real before the apps exist: each shows "Coming soon".
// Delete an entry here when its real app is registered (Read: P4, Home: P5, WhatsApp/Mail: P6).
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

struct ReadApp : ComingSoonApp { ReadApp() : ComingSoonApp("Read", Icon::BookOpen) {} };
struct HomeApp : ComingSoonApp { HomeApp() : ComingSoonApp("Home", Icon::House) {} };
struct WhatsAppApp : ComingSoonApp { WhatsAppApp() : ComingSoonApp("WhatsApp", Icon::MessageCircle) {} };
struct MailApp : ComingSoonApp { MailApp() : ComingSoonApp("Mail", Icon::Mail) {} };
struct SettingsApp : ComingSoonApp { SettingsApp() : ComingSoonApp("Settings", Icon::Settings) {} };  // P2-6 replaces it

POS_REGISTER_APP(ReadApp, {.id = "read", .name = "Read", .icon = Icon::BookOpen, .order = 10});
POS_REGISTER_APP(HomeApp, {.id = "home", .name = "Home", .icon = Icon::House, .order = 20});
POS_REGISTER_APP(WhatsAppApp, {.id = "whatsapp", .name = "WhatsApp", .icon = Icon::MessageCircle, .order = 30});
POS_REGISTER_APP(MailApp, {.id = "mail", .name = "Mail", .icon = Icon::Mail, .order = 40});
POS_REGISTER_APP(SettingsApp, {.id = "settings", .name = "Settings", .icon = Icon::Settings, .order = 50});

}  // namespace

}  // namespace pos::apps::placeholders
