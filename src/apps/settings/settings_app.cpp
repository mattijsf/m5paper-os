// Settings (docs/apps/settings.md): list of sections; Display, Power, Reading, Home Assistant, Storage,
// Developer and About are built; Wi-Fi shows a notice until the connectivity phase. Secret values are never
// shown: only key names and "present".
#include <cstdio>

#include "core/app.h"
#include "core/fs.h"
#include "core/os.h"
#include "core/version.h"
#include "hal/system.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::apps::settings {

namespace {

using ui::Type;

std::string bytesText(uint64_t b) {
  char buf[32];
  if (b >= 1024ull * 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f GB", b / (1024.0 * 1024 * 1024));
  else if (b >= 1024ull * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024));
  else std::snprintf(buf, sizeof buf, "%llu KB", static_cast<unsigned long long>(b / 1024));
  return buf;
}

std::string kbText(uint32_t b) {
  char buf[32];
  if (b >= 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024));
  else std::snprintf(buf, sizeof buf, "%u KB", static_cast<unsigned>(b / 1024));
  return buf;
}

// Each setting screen derives from this: a title and helpers for captions and saving.
class Page : public Screen {
 protected:
  Config& cfg() { return ctx().os().config(); }
  void save() { cfg().save(); }
  static ui::Label* caption(ui::Column& root, const std::string& text) {
    root.add<ui::Spacer>(ui::s2);
    auto* l = root.add<ui::Label>(text, Type::Caption, ui::secondary);
    l->maxLines = 2;
    return l;
  }
};

class DisplayPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Display");
    root.add<ui::Spacer>(ui::s2);
    auto* clean = root.add<ui::Button>("Clean screen now", ui::Button::Kind::Secondary);
    clean->onClick = [this] { ctx().os().displayManager().forceClean(); };

    caption(root, "Ghost limit: a full clean happens after this much Fast refreshing");
    auto* limit = root.add<ui::Stepper>(static_cast<int>(cfg().display.ghostLimit + 0.5f), 4, 16, 1);
    limit->onChange = [this](int v) {
      cfg().display.ghostLimit = static_cast<float>(v);
      ctx().os().applyDisplayConfig();
      save();
    };

    caption(root, "Settle delay: quiet time before text is redrawn in grays");
    int sel = cfg().display.settleMs <= 450 ? 0 : cfg().display.settleMs <= 800 ? 1 : 2;
    auto* settle = root.add<ui::Segmented>(std::vector<std::string>{"300 ms", "600 ms", "1000 ms"}, sel);
    settle->onChange = [this](int i) {
      static const int kMs[] = {300, 600, 1000};
      cfg().display.settleMs = kMs[i];
      ctx().os().applyDisplayConfig();
      save();
    };
  }
};

class PowerPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Power");
    caption(root, "Switch off after");
    const int m = cfg().power.autoOffMin;
    auto* off = root.add<ui::Segmented>(std::vector<std::string>{"5 min", "15 min", "30 min", "Never"}, m == 5 ? 0 : m == 15 ? 1 : m == 30 ? 2 : m == 0 ? 3 : 1);
    off->onChange = [this](int i) {
      static const int kMin[] = {5, 15, 30, 0};
      cfg().power.autoOffMin = kMin[i];
      save();
    };
    root.add<ui::Spacer>(ui::s2);
    auto* usb = root.add<ui::ListItem>("Stay awake on USB", "Sleeping would cut the serial link", true, ui::Icon::Power);
    usb->setToggle(cfg().power.stayAwakeOnUsb, [this](bool on) {
      cfg().power.stayAwakeOnUsb = on;
      save();
    });
    battery_ = root.add<ui::ListItem>("Battery", "", true, ui::Icon::Battery);
    update();
  }
  void onShow() override { update(); }

 private:
  void update() {
    hal::Power& p = ctx().os().platform().power;
    battery_->setTrailingText(std::to_string(p.batteryPercent()) + "%  " + std::to_string(p.batteryMv()) + " mV");
  }
  ui::ListItem* battery_ = nullptr;
};

class ReadingPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Reading");
    caption(root, "Font size");
    int fs = cfg().reader.fontSize == "S" ? 0 : cfg().reader.fontSize == "L" ? 2 : cfg().reader.fontSize == "XL" ? 3 : 1;
    auto* size = root.add<ui::Segmented>(std::vector<std::string>{"S", "M", "L", "XL"}, fs);
    size->onChange = [this](int i) {
      static const char* kSize[] = {"S", "M", "L", "XL"};
      cfg().reader.fontSize = kSize[i];
      save();
    };
    caption(root, "Margins");
    int mg = cfg().reader.margins == "narrow" ? 0 : cfg().reader.margins == "wide" ? 2 : 1;
    auto* margins = root.add<ui::Segmented>(std::vector<std::string>{"Narrow", "Normal", "Wide"}, mg);
    margins->onChange = [this](int i) {
      static const char* kM[] = {"narrow", "normal", "wide"};
      cfg().reader.margins = kM[i];
      save();
    };
    caption(root, "Line spacing");
    auto* spacing = root.add<ui::Stepper>(static_cast<int>(cfg().reader.lineSpacing * 100 + 0.5f), 110, 180, 5, [](int v) {
      char b[16];
      std::snprintf(b, sizeof b, "%d.%02d", v / 100, v % 100);
      return std::string(b);
    });
    spacing->onChange = [this](int v) {
      cfg().reader.lineSpacing = v / 100.0f;
      save();
    };
  }
};

class WifiPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Wi-Fi");
    auto* es = root.add<ui::EmptyState>(ui::Icon::WifiOff, "Wi-Fi setup", "Joining networks arrives with the connectivity update.");
    es->flex = 1;
  }
};

class HaPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Home Assistant");
    const Secrets& sec = ctx().secrets;
    root.add<ui::Spacer>(ui::s2);
    const bool urlSet = sec.present("HA_URL");
    root.add<ui::ListItem>("Base URL", urlSet ? "Set by HA_URL on the SD card" : cfg().ha.baseUrl, true, ui::Icon::House);
    root.add<ui::ListItem>("Token (HA_TOKEN)", "", true, ui::Icon::Info)->setTrailingText(sec.present("HA_TOKEN") ? "present" : "not configured");
    caption(root, sec.fileFound() ? "Keys in /sd/paperos_config (values are never shown):" : "No /sd/paperos_config on the SD card.");
    std::string names;
    for (const auto& k : sec.keys()) names += (names.empty() ? "" : ", ") + k;
    if (!names.empty()) {
      auto* l = root.add<ui::Label>(names, Type::Body);
      l->maxLines = 3;
    }
    root.add<ui::Spacer>(ui::s3);
    root.add<ui::Button>("Test connection", [this] { ctx().toast("Not available yet"); }, ui::Button::Kind::Secondary);
  }
};

class StoragePage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Storage");
    root.add<ui::Spacer>(ui::s2);
    sd_ = root.add<ui::ListItem>("SD card", "", true, ui::Icon::Folder);
    data_ = root.add<ui::ListItem>("Internal flash (/data)", "", true, ui::Icon::Folder);
    root.add<ui::Spacer>(ui::s3);
    root.add<ui::Button>("Clear caches", [this] {
      fs::removeAll("/data/cache");
      ctx().toast("Caches cleared");
      update();
    }, ui::Button::Kind::Secondary);
    update();
  }
  void onShow() override { update(); }

 private:
  void update() {
    auto sd = hal::storageInfo("sd");
    sd_->setSubtitle(sd.present ? bytesText(sd.totalBytes) + ", " + bytesText(sd.freeBytes) + " free" : "No SD card");
    auto data = hal::storageInfo("data");
    data_->setSubtitle(bytesText(data.totalBytes) + ", " + bytesText(data.freeBytes) + " free");
  }
  ui::ListItem *sd_ = nullptr, *data_ = nullptr;
};

class DeveloperPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Developer");
    root.add<ui::Spacer>(ui::s1);
    heap_ = root.add<ui::ListItem>("Heap free", "", false);
    psram_ = root.add<ui::ListItem>("PSRAM free", "", false);
    counters_ = root.add<ui::ListItem>("Refreshes since boot", "", false);
    root.add<ui::Spacer>(ui::s1);
    root.add<ui::Label>("Log", Type::Caption, ui::secondary);
    log_ = root.add<ui::Label>("", Type::Caption);
    log_->maxLines = 8;
    log_->flex = 1;
    auto* row = root.add<ui::Row>(ui::s2);
    auto* sleep = row->add<ui::Button>("Sleep test", ui::Button::Kind::Secondary);
    sleep->flex = 1;
    sleep->onClick = [this] {
      // Light sleep for up to 10 s; a touch or the timer wakes it. Reports what woke it.
      const uint32_t t0 = ctx().clock.millis();
      hal::Wake w = ctx().os().platform().power.lightSleep(10000);
      char buf[48];
      std::snprintf(buf, sizeof buf, "Woke by %s after %.1f s", w == hal::Wake::Touch ? "touch" : w == hal::Wake::Timer ? "timer" : "other",
                    (ctx().clock.millis() - t0) / 1000.0);
      ctx().toast(buf);
    };
    auto* off = row->add<ui::Button>("Power off", ui::Button::Kind::Secondary);
    off->flex = 1;
    off->onClick = [this, off] {
      auto sheet = std::make_unique<ui::Sheet>();
      ui::Column* c = sheet->content();
      c->add<ui::Label>("Power off?", Type::Title);
      c->add<ui::Label>("The picture stays on the screen. Press the side button to start again.", Type::Body, ui::secondary)->maxLines = 3;
      ui::Sheet* raw = sheet.get();
      c->add<ui::Button>("Power off", [this] { ctx().os().platform().power.powerOff(0); });
      c->add<ui::Button>("Cancel", [raw] { raw->close(); }, ui::Button::Kind::Secondary);
      off->host()->addOverlay(std::move(sheet));
    };
    root.add<ui::Spacer>(ui::s1);
    root.add<ui::Button>("Reboot", [] { hal::restart(); }, ui::Button::Kind::Secondary);
    update();
  }
  void onShow() override { update(); }

 private:
  void update() {
    hal::MemoryInfo m = hal::memoryInfo();
    heap_->setTrailingText(kbText(m.heapFree) + " / " + kbText(m.heapTotal));
    psram_->setTrailingText(kbText(m.psramFree) + " / " + kbText(m.psramTotal));
    const auto& c = ctx().os().displayManager().counts();
    counters_->setTrailingText("Q " + std::to_string(c.quality) + "  T " + std::to_string(c.text) + "  F " + std::to_string(c.fast));
    std::string tail = logTail(6);
    while (!tail.empty() && tail.back() == '\n') tail.pop_back();
    for (auto& ch : tail)
      if (ch == '\n') ch = ' ';
    log_->setText(tail);
  }
  ui::ListItem *heap_ = nullptr, *psram_ = nullptr, *counters_ = nullptr;
  ui::Label* log_ = nullptr;
};

class AboutPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("About");
    root.add<ui::Spacer>(ui::s2);
    const std::string board = hal::boardName();
    root.add<ui::ListItem>("PaperOS", kVersion, true, ui::Icon::Info);
    root.add<ui::ListItem>("Build", board == "simulator" ? std::string("simulator build") : std::string(__DATE__ " " __TIME__), false);
    root.add<ui::ListItem>("Board", board, false);
    const std::string mac = hal::macAddress();
    root.add<ui::ListItem>("Wi-Fi MAC", mac.empty() ? "not available" : mac, false);
    caption(root, "Licences");
    auto* l = root.add<ui::Label>(
        "Inter, Literata: SIL Open Font License 1.1. Lucide icons: ISC. M5GFX, M5Unified, ArduinoJson: MIT.", Type::Body);
    l->maxLines = 4;
  }
};

class ListPage : public Screen {
 public:
  void build(ui::Column& root) override {
    setTitle("Settings");
    root.add<ui::Spacer>(ui::s1);
    row(root, "Wi-Fi", ui::Icon::Wifi, [this] { ctx().nav.push<WifiPage>(); });
    row(root, "Display", ui::Icon::Sun, [this] { ctx().nav.push<DisplayPage>(); });
    row(root, "Power", ui::Icon::Power, [this] { ctx().nav.push<PowerPage>(); });
    row(root, "Reading", ui::Icon::BookOpen, [this] { ctx().nav.push<ReadingPage>(); });
    row(root, "Home Assistant", ui::Icon::House, [this] { ctx().nav.push<HaPage>(); });
    row(root, "Storage", ui::Icon::Folder, [this] { ctx().nav.push<StoragePage>(); });
    row(root, "Developer", ui::Icon::Type, [this] { ctx().nav.push<DeveloperPage>(); });
    row(root, "About", ui::Icon::Info, [this] { ctx().nav.push<AboutPage>(); });
  }

 private:
  static void row(ui::Column& root, const char* name, ui::Icon icon, std::function<void()> fn) {
    auto* item = root.add<ui::ListItem>(name, "", true, icon);
    item->setChevron();
    item->onClick = std::move(fn);
  }
};

class SettingsApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<ListPage>(); }
};

POS_REGISTER_APP(SettingsApp, {.id = "settings", .name = "Settings", .icon = Icon::Settings, .order = 50});

}  // namespace

}  // namespace pos::apps::settings
