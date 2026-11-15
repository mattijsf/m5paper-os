// __NAME__: replace this text with one line on what the app is for.
// Created by tools/new-app.sh from src/apps/_template/ (docs/APPS.md has the contract).
#include "core/app.h"
#include "ui/widgets.h"

namespace pos::apps::__ID__ {

class __CLASS__Screen : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("__NAME__");
    label_ = root.add<ui::Label>("Tap the button", ui::Type::Body);
    root.add<ui::Button>("Count", [this] {
      ++count_;
      label_->setText("Tapped " + std::to_string(count_) + " times");  // invalidates itself
    });
  }

 private:
  ui::Label* label_ = nullptr;
  int count_ = 0;
};

class __CLASS__App : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<__CLASS__Screen>(); }
};

POS_REGISTER_APP(__CLASS__App, {
    .id = "__ID__",         // unique, lowercase; also the config and cache namespace
    .name = "__NAME__",     // label under the launcher tile
    .icon = Icon::__ICON__, // from src/gen/icons.h
    .order = 90,            // position in the launcher (lower first)
});

}  // namespace pos::apps::__ID__
