#include "apps/apps.h"

#include "apps/launcher/launcher.h"
#include "apps/uitest/uitest.h"
#include "core/shell.h"

namespace pos::apps {

std::unique_ptr<Surface> createSurface(const std::string& name, Os& os) {
  if (name.empty() || AppRegistry::instance().find(name)) {
    auto shell = std::make_unique<Shell>(os);
    launcher::install(*shell);
    if (!name.empty()) shell->requestOpen(name);
    return shell;
  }
  if (auto ui = uitest::create(name, os)) return ui;
  return nullptr;
}

}  // namespace pos::apps
