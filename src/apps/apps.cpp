#include "apps/apps.h"

#include "apps/inbox/scheduled_refresh.h"
#include "apps/launcher/launcher.h"
#include "apps/uitest/uitest.h"
#include "core/shell.h"
#include "core/store.h"

namespace pos::apps {

std::unique_ptr<Surface> createSurface(const std::string& name, Os& os, bool resume) {
  if (name.empty() || AppRegistry::instance().find(name)) {
    auto shell = std::make_unique<Shell>(os);
    launcher::install(*shell);
    if (!name.empty()) {
      shell->requestOpen(name);
    } else if (resume) {
      // After a power-off the app that was open comes back (its own state is restored by the app). Once: a boot that follows
      // a crash or a reset shows the launcher.
      Store sys("/data/system.json");
      const std::string last = sys.get("lastApp", std::string());
      if (!last.empty()) {
        sys.set("lastApp", std::string());
        sys.save();
        if (AppRegistry::instance().find(last)) shell->requestOpen(last);
      }
    }
    return shell;
  }
  if (auto ui = uitest::create(name, os)) return ui;
  return nullptr;
}

std::unique_ptr<Surface> createBootSurface(const std::string& name, Os& os) {
  const bool scheduled = os.takeScheduledWake();  // always asked: it also forgets the remembered wake time
  if (scheduled && name.empty()) return inbox::createRefreshSurface(os);
  return createSurface(name, os, true);
}

}  // namespace pos::apps
