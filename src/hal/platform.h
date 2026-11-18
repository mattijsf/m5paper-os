// HAL: the bundle of interfaces one platform provides, plus the log hook.
// platform/device and platform/sim each build a Platform in main.cpp and hand it to pos::Os.
#pragma once

#include "hal/clock.h"
#include "hal/display.h"
#include "hal/http_server.h"
#include "hal/input.h"
#include "hal/net.h"
#include "hal/power.h"
#include "hal/update.h"

namespace pos::hal {

struct Platform {
  Display& display;
  Input& input;
  Clock& clock;
  Net& net;
  Power& power;
  const char* name;  // shown in the demo footer: "simulator", "M5PaperS3"
  HttpServer* http = nullptr;  // the local file server (Settings > Storage > Files over Wi-Fi); null where there is none
  Updater* update = nullptr;   // firmware update over Wi-Fi (Settings > About); null where there is none
};

// Log sink, implemented by each platform: one complete line (ends with '\n').
// stderr in the simulator, Serial on the device. Use POS_LOG (core/log.h), not this.
void logWrite(const char* line);

}  // namespace pos::hal
