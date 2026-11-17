#include "apps/common/ha_error_state.h"

#include <string>

namespace pos::apps {

ui::EmptyState* addHaErrorState(ui::Widget& holder, HaError error, std::function<void()> retry) {
  ui::EmptyState* state = nullptr;
  if (error == HaError::NoToken || error == HaError::NoUrl) {
    state = holder.add<ui::EmptyState>(ui::Icon::Cloud, "Not set up yet", "Put HA_URL and HA_TOKEN in paperos_config on the SD card.");
  } else if (error == HaError::Unauthorized) {
    state = holder.add<ui::EmptyState>(ui::Icon::TriangleAlert, "Home Assistant refused the token",
                                       "Check HA_TOKEN in paperos_config on the SD card.", "Retry", std::move(retry));
  } else {
    // "Other" is a transport error or an answer that made no sense: the address and Wi-Fi are what the user can check.
    const std::string hint = error == HaError::Other || error == HaError::None ? std::string("Check Wi-Fi and the address, then try again.")
                                                                               : haErrorText(error) + std::string(". Try again later.");
    state = holder.add<ui::EmptyState>(ui::Icon::WifiOff, "Cannot reach Home Assistant", hint, "Retry", std::move(retry));
  }
  state->flex = 1;
  return state;
}

}  // namespace pos::apps
