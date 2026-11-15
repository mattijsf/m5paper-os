#include "core/quick_panel.h"

#include <cstdio>
#include <memory>

#include "core/shell.h"
#include "ui/widgets.h"

namespace pos {

void openQuickPanel(Shell& shell) {
  if (shell.root().hasOverlay()) return;
  Os& os = shell.os();
  hal::Platform& p = os.platform();
  auto sheet = std::make_unique<ui::Sheet>();
  ui::Column* c = sheet->content();
  ui::Sheet* raw = sheet.get();

  // Wi-Fi: cached state only (the radio HAL is not touched from the UI thread while the worker may be using it).
  const bool online = os.net().online();
  const hal::WifiLink link = online ? os.net().link() : hal::WifiLink{};
  auto* wifi = c->add<ui::ListItem>("Wi-Fi", online ? (link.ssid.empty() ? "Connected" : link.ssid) : "Off until something needs it", true,
                                    online ? Icon::Wifi : Icon::WifiOff);
  wifi->setTrailingText(online ? "On" : "Off");

  // Battery detail: percent, state, voltage.
  const int pct = p.power.batteryPercent();
  const int mv = p.power.batteryMv();
  char sub[64];
  std::snprintf(sub, sizeof sub, "%s, %d.%02d V", p.power.charging() ? "Charging" : "On battery", mv / 1000, (mv % 1000) / 10);
  auto* battery = c->add<ui::ListItem>("Battery", sub, true, p.power.charging() ? Icon::BatteryCharging : pct < 20 ? Icon::BatteryLow : Icon::Battery);
  battery->setTrailingText(std::to_string(pct) + "%");

  auto* row = c->add<ui::Row>(ui::s1);
  Shell* sh = &shell;
  auto* clean = row->add<ui::Button>("Clean screen", ui::Button::Kind::Secondary);
  clean->flex = 1;
  clean->onClick = [sh, raw] {
    raw->close();
    sh->os().displayManager().forceClean();
  };
  auto* sleep = row->add<ui::Button>("Sleep now");
  sleep->flex = 1;
  sleep->onClick = [sh, raw] {
    raw->close();
    sh->sleepNow();
  };
  shell.root().addOverlay(std::move(sheet));
}

}  // namespace pos
