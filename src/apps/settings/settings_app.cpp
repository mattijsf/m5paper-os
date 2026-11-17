// Settings (docs/apps/settings.md): list of sections; Display, Power, Reading, Home Assistant (with Phone: pairing the companion
// app), Storage, Developer and About are built; Wi-Fi shows a notice until the connectivity phase. Secret values are never
// shown: only key names and "present".
#include <algorithm>
#include <cstdio>

#include "core/app.h"
#include "core/companion.h"
#include "core/file_server.h"
#include "core/fs.h"
#include "core/os.h"
#include "core/paired_owed.h"
#include "core/version.h"
#include "hal/system.h"
#include "ui/qr.h"
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
  void save() { ctx().os().saveConfigSoon(); }  // written ~1 s later (and before power-off): the tap redraws first
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
    caption(root, "While off, wake to check the inbox every");
    const int h = cfg().power.refreshHours;
    auto* refresh = root.add<ui::Segmented>(std::vector<std::string>{"Off", "1 h", "2 h", "4 h", "8 h"}, h == 1 ? 1 : h == 2 ? 2 : h == 4 ? 3 : h == 8 ? 4 : 0);
    refresh->onChange = [this](int i) {
      static const int kHours[] = {0, 1, 2, 4, 8};
      cfg().power.refreshHours = kHours[i];
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

// ---- Wi-Fi: status, scan and join, saved networks

std::string signalText(int rssi) { return std::to_string(rssi) + " dBm"; }

class WifiPasswordPage : public Page {
 public:
  explicit WifiPasswordPage(hal::WifiNetwork ap) : ap_(std::move(ap)) {}
  void build(ui::Column& root) override {
    setTitle(ap_.ssid.empty() ? "Join network" : ap_.ssid);
    root.add<ui::Spacer>(ui::s2);
    root.add<ui::Label>("Password", Type::Caption, ui::secondary);
    field_ = root.add<ui::TextField>("Network password");
    field_->setMasked(true);
    show_ = root.add<ui::Button>("Show", [this] {
      masked_ = !masked_;
      field_->setMasked(masked_);
      show_->setText(masked_ ? "Show" : "Hide");
    }, ui::Button::Kind::Text);
    status_ = root.add<ui::Label>("", Type::Body);  // a result is content
    status_->maxLines = 3;
  }
  void onShow() override {
    auto panel = std::make_unique<ui::KeyboardPanel>();
    panel_ = panel.get();
    ui::bindKeyboard(*panel->keyboard(), *field_, [this] { join(); }, 63);
    field_->host()->addOverlay(std::move(panel));
  }
  void onHide() override {
    if (panel_ && field_ && field_->host()) field_->host()->closeOverlay(panel_);
    panel_ = nullptr;
  }

 private:
  void join() {
    if (joining_) return;
    joining_ = true;
    status_->setText("Joining...");
    const std::string ssid = ap_.ssid, pass = field_->text();
    NetService* net = &ctx().os().net();
    ctx().async([net, ssid, pass] { return net->connectTo(ssid, pass, true); }, [this](NetService::Result r) {
      joining_ = false;
      if (r.ok) {
        ctx().toast("Joined " + r.ssid + ", saved");
        ctx().nav.pop();
      } else {
        status_->setText(r.error + ". Check the password.");
      }
    });
  }
  hal::WifiNetwork ap_;
  ui::TextField* field_ = nullptr;
  ui::Button* show_ = nullptr;
  ui::Label* status_ = nullptr;
  ui::KeyboardPanel* panel_ = nullptr;
  bool masked_ = true, joining_ = false;
};

class WifiScanPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Join a network");
    root.add<ui::Spacer>(ui::s1);
    info_ = root.add<ui::Label>("Scanning... (2.4 GHz networks only)", Type::Caption, ui::secondary);
    list_ = root.add<ui::Column>(0);
  }
  void onShow() override { scan(); }

 private:
  void scan() {
    NetService* net = &ctx().os().net();
    info_->setText("Scanning... (2.4 GHz networks only)");
    ctx().async([net] { return net->scan(); }, [this](std::vector<hal::WifiNetwork> aps) {
      std::sort(aps.begin(), aps.end(), [](const hal::WifiNetwork& a, const hal::WifiNetwork& b) { return a.rssi > b.rssi; });
      // one row per SSID (the strongest access point), at most seven rows
      std::vector<hal::WifiNetwork> rows;
      for (const auto& ap : aps) {
        if (ap.ssid.empty()) continue;
        bool dup = false;
        for (const auto& r : rows)
          if (r.ssid == ap.ssid) dup = true;
        if (!dup) rows.push_back(ap);
        if (rows.size() == 7) break;
      }
      while (!list_->children().empty()) list_->remove(list_->children().back().get());
      info_->setText(rows.empty() ? "No networks found. The device sees 2.4 GHz only." : "Tap a network to join it.");
      for (const auto& ap : rows) {
        auto* item = list_->add<ui::ListItem>(ap.ssid, signalText(ap.rssi) + "  ch " + std::to_string(ap.channel), true,
                                              ap.secure ? ui::Icon::Wifi : ui::Icon::Wifi);
        item->setTrailingText(ap.secure ? "Secured" : "Open");
        item->onClick = [this, ap] { choose(ap); };
      }
      list_->host()->relayout();
    });
  }
  void choose(const hal::WifiNetwork& ap) {
    // A known network joins without asking; an open one too; else ask for the password.
    for (const auto& k : ctx().os().net().known())
      if (k.ssid == ap.ssid) {
        NetService* net = &ctx().os().net();
        const std::string ssid = k.ssid, pass = k.password;
        ctx().async([net, ssid, pass] { return net->connectTo(ssid, pass, false); }, [this](NetService::Result r) {
          ctx().toast(r.ok ? "Joined " + r.ssid : r.error);
        });
        return;
      }
    if (!ap.secure) {
      NetService* net = &ctx().os().net();
      const std::string ssid = ap.ssid;
      ctx().async([net, ssid] { return net->connectTo(ssid, "", true); }, [this](NetService::Result r) {
        ctx().toast(r.ok ? "Joined " + r.ssid : r.error);
      });
      return;
    }
    ctx().nav.push<WifiPasswordPage>(ap);
  }
  ui::Label* info_ = nullptr;
  ui::Column* list_ = nullptr;
};

class WifiSavedPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Saved networks");
    root.add<ui::Spacer>(ui::s1);
    list_ = root.add<ui::Column>(0);
    fill();
  }

 private:
  void fill() {
    while (!list_->children().empty()) list_->remove(list_->children().back().get());
    auto known = ctx().os().net().known();
    if (known.empty()) {
      list_->add<ui::Label>("No saved networks. Add WIFI_SSID and WIFI_PASSWORD to /sd/paperos_config, or join one from the scan.", Type::Body, ui::secondary)->maxLines = 4;
    }
    for (const auto& k : known) {
      auto* item = list_->add<ui::ListItem>(k.ssid, k.fromCard ? "From the SD card" : "Added on this device", true, ui::Icon::Wifi);
      if (k.fromCard) {
        item->setTrailingText(k.open() ? "Open" : "Secured");
      } else {
        item->setTrailingText("Forget");
        const std::string ssid = k.ssid;
        item->onClick = [this, ssid] {
          ctx().os().net().store().forget(ssid);
          ctx().os().net().store().save();
          ctx().toast("Forgot " + ssid);
          ctx().timer(0, [this] {  // not inside the handler of the row that is being removed
            fill();
            list_->host()->relayout();
          });
        };
      }
    }
  }
  ui::Column* list_ = nullptr;
};

class WifiPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Wi-Fi");
    root.add<ui::Spacer>(ui::s2);
    status_ = root.add<ui::ListItem>("Status", "", true, ui::Icon::Wifi);
    root.add<ui::Spacer>(ui::s2);
    auto* scan = root.add<ui::ListItem>("Join a network", "Scan for networks", true, ui::Icon::RefreshCw);
    scan->setChevron();
    scan->onClick = [this] { ctx().nav.push<WifiScanPage>(); };
    auto* saved = root.add<ui::ListItem>("Saved networks", "", true, ui::Icon::Folder);
    saved->setChevron();
    saved->onClick = [this] { ctx().nav.push<WifiSavedPage>(); };
    root.add<ui::Spacer>(ui::s3);
    root.add<ui::Button>("Connect now", [this] { connectNow(); }, ui::Button::Kind::Secondary);
    root.add<ui::Spacer>(ui::s2);
    note_ = root.add<ui::Label>("", Type::Body);
    note_->maxLines = 5;
    update();
  }
  void onShow() override { update(); }

 private:
  void connectNow() {
    note_->setText("Connecting...");
    NetService* net = &ctx().os().net();
    ctx().async([net] { return net->ensureConnected(); }, [this](NetService::Result r) {
      char buf[200];
      if (r.ok) std::snprintf(buf, sizeof buf, "Connected to %s in %.1f s%s.", r.ssid.c_str(), r.ms / 1000.0, r.fast ? " (fast path)" : "");
      else std::snprintf(buf, sizeof buf, "%s. %s", r.error.c_str(), r.scan.c_str());
      note_->setText(buf);
      update();
    });
  }
  void update() {
    hal::WifiLink l = ctx().os().net().link();
    if (l.connected) {
      status_->setSubtitle("Connected to " + l.ssid + ", " + signalText(l.rssi));
      status_->setTrailingText(l.ip);
    } else {
      status_->setSubtitle("Off: turns on when an app needs it");
      status_->setTrailingText("");
    }
  }
  ui::ListItem* status_ = nullptr;
  ui::Label* note_ = nullptr;
};

// Settings > Home Assistant > Phone: pairing with the companion app (docs/COMPANION.md section 2). The QR code carries the Paper's
// public key, a pairing id and the Home Assistant address; the phone answers through the relay; both screens show a six-digit
// code the user compares. Keys are never shown or logged.
class PhonePage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Phone");
    holder_ = root.add<ui::Column>(ui::s1);
    holder_->flex = 1;
    show();
  }

  void onHide() override { polling_ = false; }

  std::string debugState() const override { return "phone=" + state_; }

 private:
  void clear() {
    while (!holder_->children().empty()) holder_->remove(holder_->children().back().get());
  }
  ui::Label* text(const std::string& t, Type type = Type::Body, Color color = ui::ink, int lines = 3) {
    auto* l = holder_->add<ui::Label>(t, type, color);
    l->maxLines = lines;
    return l;
  }

  void show() {
    clear();
    polling_ = false;
    companion::Pairing p;
    holder_->add<ui::Spacer>(ui::s2);
    if (companion::loadPairing(p)) {
      state_ = "paired";
      holder_->add<ui::ListItem>("Paired phone", p.phoneName, true, ui::Icon::Check);
      // D23: a copy on the Paper goes to the phone; what the phone shares arrives before a paste.
      auto* clip = holder_->add<ui::ListItem>("Share the clipboard", "Copy on one, paste on the other", true, ui::Icon::FileText);
      clip->setToggle(cfg().phone.shareClipboard, [this](bool on) {
        cfg().phone.shareClipboard = on;
        save();
      });
      holder_->add<ui::Spacer>(ui::s2);
      text("WhatsApp and Proton Mail notifications from this phone come here end-to-end encrypted. Home Assistant only relays them.",
           Type::Body, ui::secondary, 4);
      holder_->add<ui::Spacer>(ui::s1)->flex = 1;
      holder_->add<ui::Button>("Unpair", [this] { confirmUnpair(); }, ui::Button::Kind::Secondary);
    } else {
      state_ = "none";
      text("No phone paired", Type::Title, ui::ink, 1);
      holder_->add<ui::Spacer>(ui::s1);
      text("Install the PaperOS app on your phone, open it and choose Pair. Then tap Pair phone here and scan the code.", Type::Body,
           ui::secondary, 4);
      holder_->add<ui::Spacer>(ui::s1)->flex = 1;
      holder_->add<ui::Button>("Pair phone", [this] { startPairing(); });
    }
    holder_->add<ui::Spacer>(ui::s2);
    holder_->host()->relayout();
  }

  void startPairing() {
    const std::string url = ctx().os().net().haBaseUrl();
    if (url.empty()) {
      ctx().toast("No Home Assistant address (HA_URL)");
      return;
    }
    pending_ = companion::startPairing();
    clear();
    state_ = "scan";
    holder_->add<ui::Spacer>(ui::s1);
    text("Scan this code with the PaperOS app", Type::BodyStrong, ui::ink, 2);
    auto* qr = holder_->add<ui::QrCode>(companion::qrText(pending_.pid, pending_.keys.pk, url), 492);
    qr->flex = 1;
    status_ = text("Waiting for your phone...", Type::Body, ui::secondary, 2);
    status_->align = ui::TextAlign::Center;
    holder_->add<ui::Button>("Cancel", [this] { show(); }, ui::Button::Kind::Secondary);
    holder_->add<ui::Spacer>(ui::s2);
    holder_->host()->relayout();
    polling_ = true;
    pollStartMs_ = ctx().clock.millis();
    ctx().timer(3000, [this] { poll(); });  // scanning takes the phone a few seconds anyway
  }

  // Every 3 s while the code is shown, for 15 minutes at most (the relay keeps a hello that long).
  void poll() {
    if (!polling_ || busy_) return;
    if (ctx().clock.millis() - pollStartMs_ > 15 * 60 * 1000) {
      polling_ = false;
      status_->setText("No answer from the phone. Cancel and try again.");
      return;
    }
    busy_ = true;
    HaClient ha = ctx().ha();
    const companion::PendingPairing pending = pending_;
    ctx().async(
        [ha, pending]() mutable {
          HaResult r = ha.get(std::string("/api/states/") + companion::kPairingEntity);
          companion::HelloResult h;
          if (r.ok()) h = companion::readHello(pending, r.body);
          return std::make_pair(r, h);
        },
        [this](std::pair<HaResult, companion::HelloResult> res) {
          busy_ = false;
          if (!polling_) return;
          if (res.second.found) {
            polling_ = false;
            showCode(res.second);
            return;
          }
          if (res.first.error == HaError::NotFound) {
            polling_ = false;
            status_->setText("The relay is missing in Home Assistant.");
            return;
          }
          if (!res.first.ok()) status_->setText(std::string("Waiting for your phone... (") + haErrorText(res.first.error) + ")");
          ctx().timer(3000, [this] { poll(); });
        });
  }

  void showCode(const companion::HelloResult& h) {
    hello_ = h;
    clear();
    state_ = "code";
    holder_->add<ui::Spacer>(ui::s3);
    text(h.phoneName, Type::Title, ui::ink, 1);
    text("wants to pair. Does your phone show this code?", Type::Body, ui::secondary, 2);
    holder_->add<ui::Spacer>(ui::s3);
    auto* code = text(h.code, Type::Headline, ui::ink, 1);
    code->align = ui::TextAlign::Center;
    holder_->add<ui::Spacer>(ui::s3);
    text("Only confirm when the codes are the same: otherwise someone else's key would be used.", Type::Caption, ui::secondary, 3);
    holder_->add<ui::Spacer>(ui::s1)->flex = 1;
    holder_->add<ui::Button>("Codes match", [this] { confirm(); });
    holder_->add<ui::Button>("Cancel", [this] { show(); }, ui::Button::Kind::Secondary);
    holder_->add<ui::Spacer>(ui::s2);
    holder_->host()->relayout();
  }

  void confirm() {
    companion::Pairing p = hello_.pairing;
    p.tx = 0;
    p.rx = 0;
    const std::string paired = companion::pairedEnvelope(p);  // counter 1
    if (paired.empty() || !companion::savePairing(p)) {
      ctx().toast("Could not save the pairing");
      return;
    }
    companion::saveNotifs(companion::Notifs{});
    // Owed until a push reaches the relay; the inbox pushes it again when Wi-Fi comes up (core/paired_owed.h).
    companion::markPairedOwed(ctx().clock.now());
    ctx().log("paired with a phone");  // never the keys
    HaClient ha = ctx().ha();
    const uint32_t t0 = ctx().clock.millis();
    ctx().async(
        [ha, paired]() mutable {
          HaResult r = ha.callService("script", "paperos_relay_push", paired);
          ha.callService("script", "paperos_relay_ack", companion::ackBody("pairing", 0));
          return r;
        },
        [this, t0](HaResult r) {
          const double sec = (ctx().clock.millis() - t0) / 1000.0;
          POS_LOG("companion", "paired to the phone: %s, %.1f s", r.ok() ? "pushed" : haErrorText(r.error), sec);
          if (r.ok()) companion::markPairedSent();
          ctx().toast(r.ok() ? "Phone paired" : std::string("Paired; the phone gets it when Wi-Fi is up: ") + haErrorText(r.error));
          show();
        });
  }

  void confirmUnpair() {
    auto sheet = std::make_unique<ui::Sheet>();
    ui::Sheet* raw = sheet.get();
    ui::Column* c = sheet->content();
    c->add<ui::Label>("Unpair the phone?", Type::Title);
    c->add<ui::Label>("Its messages stop coming here. Pairing again needs the phone.", Type::Body, ui::secondary)->maxLines = 2;
    c->add<ui::Button>("Unpair", [this, raw] {
      raw->close();
      unpair();
    });
    c->add<ui::Button>("Cancel", [raw] { raw->close(); }, ui::Button::Kind::Secondary);
    holder_->host()->addOverlay(std::move(sheet));
  }

  void unpair() {
    companion::Pairing p;
    std::string body;
    if (companion::loadPairing(p)) body = companion::unpairEnvelope(p);
    companion::forgetPairing();
    ctx().log("unpaired");
    show();
    ctx().toast("Phone unpaired");
    if (body.empty()) return;
    HaClient ha = ctx().ha();
    ctx().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); }, [](HaResult) {});  // best effort
  }

  ui::Column* holder_ = nullptr;
  ui::Label* status_ = nullptr;
  std::string state_;
  companion::PendingPairing pending_;
  companion::HelloResult hello_;
  bool polling_ = false, busy_ = false;
  uint32_t pollStartMs_ = 0;
};

class HaPage : public Page {
 private:
  // GET <HA_URL>/api/ with the token: shows the HTTP status and the time it took. The token and the Authorization
  // header are never logged or shown.
  void test() {
    result_->setText("Testing...");
    const uint32_t t0 = ctx().clock.millis();
    HaClient ha = ctx().ha();
    ctx().async([ha]() mutable { return ha.get("/api/"); }, [this, t0](HaResult r) {
      char buf[160];
      const double sec = (ctx().clock.millis() - t0) / 1000.0;
      if (r.error == HaError::NoToken) std::snprintf(buf, sizeof buf, "No HA_TOKEN in /sd/paperos_config.");
      else if (r.status == 0) std::snprintf(buf, sizeof buf, "%s (%.1f s): %s", haErrorText(r.error), sec, r.detail.c_str());
      else if (r.ok()) std::snprintf(buf, sizeof buf, "HTTP %d in %.1f s: %s", r.status, sec, r.body.substr(0, 60).c_str());
      else std::snprintf(buf, sizeof buf, "%s: HTTP %d in %.1f s", haErrorText(r.error), r.status, sec);
      result_->setText(buf);
      POS_LOG("ha", "test: status=%d %.1f s", r.status, sec);
    });
  }
  ui::Label* result_ = nullptr;

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
    result_ = root.add<ui::Label>("", Type::Body);
    result_->maxLines = 3;
    root.add<ui::Spacer>(ui::s2);
    root.add<ui::Button>("Test connection", [this] { test(); }, ui::Button::Kind::Secondary);
    root.add<ui::Spacer>(ui::s3);
    companion::Pairing p;
    const bool paired = companion::loadPairing(p);
    auto* phone = root.add<ui::ListItem>("Phone", paired ? "Paired: " + p.phoneName : "Not paired", true, ui::Icon::MessageCircle);
    phone->setChevron();
    phone->onClick = [this] { ctx().nav.push<PhonePage>(); };
  }
};

// Files over Wi-Fi (docs/apps/settings.md, DECISIONS.md D30): while this page is open the Paper serves its books folder on the
// home Wi-Fi, as WebDAV (Finder: Go > Connect to Server) and as an upload page for a browser, behind a one-time code shown here.
// Leaving the page stops it; so do ten minutes without a request.
class FilesPage : public Page {
 public:
  static constexpr uint32_t kIdleStopMs = 10u * 60 * 1000;

  void build(ui::Column& root) override {
    setTitle("Files over Wi-Fi");
    root.add<ui::Spacer>(ui::s2);
    state_ = root.add<ui::Label>("", Type::Body);
    state_->maxLines = 2;
    root.add<ui::Spacer>(ui::s2);
    addrCap_ = root.add<ui::Label>("Address", Type::Caption, ui::secondary);
    addr_ = root.add<ui::Label>("", Type::Title);
    root.add<ui::Spacer>(ui::s1);
    codeCap_ = root.add<ui::Label>("Code (the password)", Type::Caption, ui::secondary);
    code_ = root.add<ui::Label>("", Type::Headline);
    root.add<ui::Spacer>(ui::s2);
    help_ = root.add<ui::Label>("Mac: Finder > Go > Connect to Server, then the address. Name: paper, password: the code.\n"
                                "Phone or any browser: open the address.",
                                Type::Body);
    help_->maxLines = 5;
    help_->keepLineBreaks = true;
    root.add<ui::Spacer>(ui::s2);
    activity_ = root.add<ui::Label>("", Type::Body);
    activity_->maxLines = 2;
    root.add<ui::Spacer>(0, 1);
    button_ = root.add<ui::Button>("Stop", [this] {
      if (running_) ctx().nav.pop();  // leaving stops the server (onHide)
      else start();
    }, ui::Button::Kind::Secondary);
    root.add<ui::Spacer>(ui::s2);
    showRunning(false);
  }
  void onShow() override {
    if (!running_ && !starting_) start();
  }
  void onHide() override { stopServer(""); }
  // A screen can also go without onHide (the whole app replaced, `>set screen`): then the app's context may already be gone, so
  // the server is stopped through the pointer kept at start, nothing else.
  ~FilesPage() override {
    if (running_ && http_) http_->stop();
  }

  std::string debugState() const override {
    return std::string(running_ ? "running" : "stopped") + "|" + state_->text() + "|" + activity_->text();
  }

 private:
  void showRunning(bool on) {
    addrCap_->visible = addr_->visible = codeCap_->visible = code_->visible = help_->visible = on;
    button_->setText(on ? "Stop" : "Start again");
    button_->visible = on || !starting_;
    if (auto* h = state_->host()) h->relayout();
  }

  void start() {
    hal::HttpServer* http = ctx().os().platform().http;
    if (!http) {
      state_->setText("Not available on this device");
      return;
    }
    starting_ = true;
    state_->setText("Joining Wi-Fi...");
    activity_->setText("");
    showRunning(false);
    NetService* net = &ctx().os().net();
    ctx().async([net] { return net->ensureConnected(); }, [this, http](NetService::Result r) {
      starting_ = false;
      if (!r.ok) {
        state_->setText("No Wi-Fi: " + r.error);
        showRunning(false);
        return;
      }
      hal::Clock* clock = &ctx().os().platform().clock;
      code_text_ = files::newCode();
      server_ = std::make_shared<files::FileServer>("/sd/books", code_text_, [clock] { return clock->millis(); });
      auto srv = server_;
      http_ = http;
      if (!http->start([srv](hal::ServerRequest& q) { return srv->handle(q); })) {
        state_->setText("Could not start the file server");
        showRunning(false);
        return;
      }
      running_ = true;
      const bool sim = std::string(ctx().os().platform().name) == "simulator";
      const std::string ip = sim ? "localhost" : ctx().os().net().link().ip;
      addr_->setText("http://" + ip + (http->port() == 80 ? "" : ":" + std::to_string(http->port())));
      code_->setText(code_text_.substr(0, 3) + " " + code_text_.substr(3));
      state_->setText("On while this page is open. The books folder is shared.");
      showRunning(true);
      tick();
    });
  }

  // Once a second: what arrived, Wi-Fi and the device kept up, the idle stop.
  void tick() {
    if (!running_ || !server_) return;
    ctx().os().net().keepAlive();
    ctx().os().keepAwake();
    const files::Stats s = server_->stats();
    std::string a;
    if (s.busy) {
      a = "Receiving " + s.receiving + ", " + bytesText(s.receivingBytes);
    } else if (!s.last.empty()) {
      a = s.last;
      if (s.received > 0) a += "\n" + std::to_string(s.received) + (s.received == 1 ? " file, " : " files, ") + bytesText(s.bytes) + " received";
    }
    if (a != activity_->text()) activity_->setText(a);
    const uint32_t idle = ctx().os().platform().clock.millis() - server_->lastRequestMs();
    if (!s.busy && idle >= kIdleStopMs) {
      stopServer("Stopped after 10 minutes without use");
      return;
    }
    ctx().timer(1000, [this] { tick(); });
  }

  void stopServer(const std::string& why) {
    if (!running_) return;
    running_ = false;
    if (http_) http_->stop();
    server_.reset();
    if (!why.empty()) {
      state_->setText(why);
      showRunning(false);
    }
  }

  ui::Label *state_ = nullptr, *addrCap_ = nullptr, *addr_ = nullptr, *codeCap_ = nullptr, *code_ = nullptr, *help_ = nullptr,
            *activity_ = nullptr;
  ui::Button* button_ = nullptr;
  std::shared_ptr<files::FileServer> server_;
  hal::HttpServer* http_ = nullptr;
  std::string code_text_;
  bool running_ = false, starting_ = false;
};

class StoragePage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Storage");
    root.add<ui::Spacer>(ui::s2);
    auto* files = root.add<ui::ListItem>("Files over Wi-Fi", "Add or delete books from a computer or phone", true, ui::Icon::Wifi);
    files->setChevron();
    files->onClick = [this] { ctx().nav.push<FilesPage>(); };
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

// Display lab (docs/DISPLAY.md "Test patterns", task P7-1): what only eyes on the panel can judge. A gray ramp, a ghosting test
// (checkerboard, then white, both as Fast passes; Clean = the Quality pass), text samples in every gray and size, and the
// ramp left in its Fast state ("Fast": pushed as Hint::Page, Fast with no settle pass) to see what the panel driver makes of
// grays in Fast mode: an ordered dither gives 16 bands of rising brightness, a threshold gives black bands and white bands.
class LabView : public ui::Widget {
 public:
  enum class Mode { Ramp, Checker, White, Text, FastRamp };
  void setMode(Mode m, Hint h) {
    mode_ = m;
    invalidate(h);
  }
  Mode mode() const { return mode_; }
  void draw(Canvas& c) override {
    const int x = frame.x, y = frame.y, w = frame.w, h = frame.h;
    if (mode_ == Mode::Ramp || mode_ == Mode::FastRamp) {
      // 16 steps, black at the top, each labelled with its level (the label in the colour that contrasts). The Fast ramp is
      // upside down (white at the top): coming from Ramp every band then differs, so the Fast push is not skipped as
      // "nothing changed" (it was, on the device: the panel kept the Text-mode ramp).
      const int step = h / 16;
      for (int i = 0; i < 16; ++i) {
        const int g = mode_ == Mode::FastRamp ? 15 - i : i;
        c.fillRect(x, y + i * step, w, step, gray(g));
        ui::drawText(c, "gray(" + std::to_string(g) + ")", x + 12, y + i * step + step / 2, Type::Caption, g < 8 ? ui::paper : ui::ink, ui::Anchor::MiddleLeft, gray(g));
      }
    } else if (mode_ == Mode::Checker) {
      const int cell = 41;  // 12 columns of 41 px in 492
      for (int row = 0; row * cell < h; ++row)
        for (int col = 0; col * cell < w; ++col)
          if ((row + col) % 2 == 0) c.fillRect(x + col * cell, y + row * cell, std::min(cell, w - col * cell), std::min(cell, h - row * cell), ui::ink);
    } else if (mode_ == Mode::Text) {
      int ty = y + 4;
      auto line = [&](const std::string& t, Type type, Color col, int advance) {
        ui::drawText(c, t, x, ty, type, col, ui::Anchor::TopLeft, bg());
        ty += advance;
      };
      for (int g : {0, 3, 5, 7, 9}) line("Caption 18 in gray(" + std::to_string(g) + "): The quick brown fox", Type::Caption, gray(g), 26);
      ty += 6;
      for (int g : {0, 3, 5, 7}) line("Body 22 in gray(" + std::to_string(g) + "): quick brown fox", Type::Body, gray(g), 32);
      ty += 6;
      line("Read S 24: It is a truth universally", Type::ReadS, ui::ink, 34);
      line("Read M 28: It is a truth universally", Type::ReadM, ui::ink, 40);
      line("Read L 32: It is a truth univers", Type::ReadL, ui::ink, 46);
      line("Read XL 38: It is a truth", Type::ReadXL, ui::ink, 54);
      ty += 8;
      for (int g : {0, 7, 9, 11, 13}) {  // 1 px hairlines in several grays
        c.drawFastHLine(x, ty, w - 130, gray(g));
        ui::drawText(c, "line gray(" + std::to_string(g) + ")", x + w, ty, Type::Caption, ui::ink, ui::Anchor::MiddleRight, bg());
        ty += 26;
      }
    }  // White: the cleared frame is the pattern
  }

 private:
  Mode mode_ = Mode::Ramp;
};

class DisplayLabPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Display lab");
    root.add<ui::Spacer>(ui::s1);
    auto* modes = root.add<ui::Segmented>(std::vector<std::string>{"Ramp", "Check", "White", "Text", "Fast"}, 0);  // Check = the checkerboard
    root.add<ui::Spacer>(ui::s1);
    view_ = root.add<LabView>();
    view_->flex = 1;
    root.add<ui::Spacer>(ui::s1);
    counters_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    auto* row = root.add<ui::Row>(ui::s2);
    auto* clean = row->add<ui::Button>("Clean (Quality)", ui::Button::Kind::Secondary);
    clean->flex = 1;
    clean->onClick = [this] {
      ctx().os().displayManager().forceClean();
      update();
    };
    auto* count = row->add<ui::Button>("Update counters", ui::Button::Kind::Secondary);
    count->flex = 1;
    count->onClick = [this] { update(); };
    root.add<ui::Spacer>(ui::s1);
    modes->onChange = [this](int i) {
      // Checker and White are pure black and white: Feedback = one Fast pass, so going Checker -> White shows the ghost a Fast pass leaves.
      const LabView::Mode m = static_cast<LabView::Mode>(i);
      // Fast: the ramp as a Page push, which stays in Fast (black and white) until the next push. Check the counters line: a
      // Page push can be the reader's every-10th Quality clean, then the ramp is in 16 grays and shows nothing.
      const Hint h = m == LabView::Mode::Checker || m == LabView::Mode::White ? Hint::Feedback
                     : m == LabView::Mode::FastRamp ? Hint::Page : Hint::Content;
      view_->setMode(m, h);
      update();
    };
    update();
  }
  std::string debugState() const override { return "lab=" + std::to_string(static_cast<int>(view_->mode())); }

 private:
  void update() {
    const auto& c = ctx().os().displayManager().counts();
    char buf[96];
    std::snprintf(buf, sizeof buf, "Pushes: Quality %u  Text %u  Fast %u   ghost score %.1f / %.0f", static_cast<unsigned>(c.quality), static_cast<unsigned>(c.text),
                  static_cast<unsigned>(c.fast), ctx().os().displayManager().ghostScore(), cfg().display.ghostLimit);
    counters_->setText(buf);
  }
  LabView* view_ = nullptr;
  ui::Label* counters_ = nullptr;
};

class DeveloperPage : public Page {
 public:
  void build(ui::Column& root) override {
    setTitle("Developer");
    root.add<ui::Spacer>(ui::s1);
    auto* lab = root.add<ui::ListItem>("Display lab", "Gray ramp, ghost test, text samples", false);
    lab->setChevron();
    lab->onClick = [this] { ctx().nav.push<DisplayLabPage>(); };
    heap_ = root.add<ui::ListItem>("Memory free", "", false);
    counters_ = root.add<ui::ListItem>("Refreshes since boot", "", false);
    battery_ = root.add<ui::ListItem>("Battery", "", false);
    uptime_ = root.add<ui::ListItem>("Time since boot", "", false);
    plog_ = root.add<ui::ListItem>("Power log", "", false);
    plog_->setToggle(cfg().power.log, [this](bool on) {
      cfg().power.log = on;
      save();
      update();
    });
    root.add<ui::Spacer>(ui::s1);
    root.add<ui::Label>("Log", Type::Caption, ui::secondary);
    log_ = root.add<ui::Label>("", Type::Caption);
    log_->maxLines = 2;
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
    auto* row2 = root.add<ui::Row>(ui::s2);
    auto* http = row2->add<ui::Button>("HTTP test", [this] { httpTest(); }, ui::Button::Kind::Secondary);
    http->flex = 1;
    auto* reboot = row2->add<ui::Button>("Reboot", [] { hal::restart(); }, ui::Button::Kind::Secondary);
    reboot->flex = 1;
    update();
  }
  void onShow() override { update(); }

 private:
  // HTTPS GET of a public page: brings Wi-Fi up if needed; shows the status, time and free heap right after.
  void httpTest() {
    hal::HttpRequest req;
    req.url = "https://www.example.com/";
    ctx().toast("HTTP test...");
    const uint32_t t0 = ctx().clock.millis();
    ctx().http(req, [this, t0](hal::HttpResponse r) {
      char buf[120];
      const double sec = (ctx().clock.millis() - t0) / 1000.0;
      if (r.status == 0) std::snprintf(buf, sizeof buf, "HTTP test failed: %s", r.error.c_str());
      else std::snprintf(buf, sizeof buf, "HTTP %d, %u bytes in %.1f s, heap %u KB free", r.status, static_cast<unsigned>(r.body.size()), sec,
                         static_cast<unsigned>(hal::memoryInfo().heapFree / 1024));
      ctx().toast(buf);
      POS_LOG("net", "http test: status=%d %.1f s", r.status, sec);
    });
  }

  void update() {
    hal::MemoryInfo m = hal::memoryInfo();
    heap_->setTrailingText(kbText(m.heapFree) + " heap, " + kbText(m.psramFree) + " PSRAM");
    const auto& c = ctx().os().displayManager().counts();
    counters_->setTrailingText("Q " + std::to_string(c.quality) + "  T " + std::to_string(c.text) + "  F " + std::to_string(c.fast));
    {
      hal::Power& pw = ctx().os().platform().power;
      char b[64];
      std::snprintf(b, sizeof b, "%d mV  %d%%  %s", pw.batteryMv(), pw.batteryPercent(), pw.usbConnected() || pw.charging() ? "USB" : "battery");
      battery_->setTrailingText(b);
      const uint32_t s = ctx().clock.millis() / 1000;
      std::snprintf(b, sizeof b, "%u h %02u min", static_cast<unsigned>(s / 3600), static_cast<unsigned>(s / 60 % 60));
      uptime_->setTrailingText(b);
      PowerLog& pl = ctx().os().powerLog();
      plog_->setSubtitle(cfg().power.log ? std::to_string(pl.lines()) + (pl.lines() == 1 ? " line" : " lines") + " since boot, Wi-Fi " + std::to_string(pl.wifiSeconds()) + " s" : "Off");
    }
    std::string tail = logTail(6);
    while (!tail.empty() && tail.back() == '\n') tail.pop_back();
    for (auto& ch : tail)
      if (ch == '\n') ch = ' ';
    log_->setText(tail);
  }
  ui::ListItem *heap_ = nullptr, *counters_ = nullptr, *battery_ = nullptr, *uptime_ = nullptr, *plog_ = nullptr;
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
