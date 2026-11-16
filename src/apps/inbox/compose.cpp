// WhatsApp reply screen (compose.h). The send is one encrypted `reply` envelope pushed to script.paperos_relay_push (COMPANION.md).
#include "apps/inbox/compose.h"

#include <memory>

#include "core/clipboard.h"
#include "core/companion.h"
#include "ui/keyboard.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::whatsapp {

namespace {

using ui::Type;

class ComposeScreen : public Screen {
 public:
  ComposeScreen(Target to, std::string text) : to_(std::move(to)), initial_(std::move(text)) {}

  void build(ui::Column& root) override {
    setTitle("Reply");
    auto* col = root.add<ui::Column>(ui::s1);
    col->add<ui::Spacer>(ui::s1);
    auto* toRow = col->add<ui::Row>(ui::s1);
    toRow->add<ui::Label>("To", Type::Body, ui::secondary);
    auto* name = toRow->add<ui::Label>(to_.name, Type::BodyStrong);  // who it goes to is content: ink
    name->flex = 1;
    col->add<ui::Spacer>(ui::s1);
    field_ = col->add<ui::TextField>("Message");
    field_->setLines(6);
    field_->setMaxChars(kMaxChars);
    field_->append(clipboard::clean(initial_));
    counter_ = col->add<ui::Label>("", Type::Caption, ui::secondary);
    col->add<ui::Spacer>(ui::s1);
    auto* buttons = col->add<ui::Row>(ui::s2);
    paste_ = buttons->add<ui::Button>("Paste", [this] { paste(); }, ui::Button::Kind::Secondary);
    send_ = buttons->add<ui::Button>("Send", [this] { confirm(); });
    send_->flex = 1;
    field_->onChange = [this] { update(); };
  }

  void onShow() override {
    auto panel = std::make_unique<ui::KeyboardPanel>();
    panel_ = panel.get();
    ui::bindKeyboard(*panel->keyboard(), *field_, {}, 4 * kMaxChars);  // Enter types a line feed; the field counts characters
    field_->host()->addOverlay(std::move(panel));
    update();
  }

  void onHide() override {
    if (panel_ && field_ && field_->host()) field_->host()->closeOverlay(panel_);
    panel_ = nullptr;
  }

  std::string debugState() const override {
    return "chars=" + std::to_string(ui::TextField::charCount(field_->text())) + (busy_ ? "|sending" : "");
  }

 private:
  void update() {
    counter_->setText(std::to_string(ui::TextField::charCount(field_->text())) + " / " + std::to_string(kMaxChars));
    setEnabled(send_, !busy_ && !trimmed(field_->text()).empty());
    setEnabled(paste_, !busy_ && (!clipboard::empty() || clipboard::syncing()));  // syncing: the phone may have something
  }

  // What the phone shared last comes first (clipboard sync, D23): read the relay, then paste.
  void paste() {
    if (pulling_) return;
    if (!clipboard::syncing()) {
      pasteNow();
      return;
    }
    pulling_ = true;
    paste_->setText("Paste...");
    std::weak_ptr<bool> alive = alive_;
    clipboard::pull([this, alive](bool) {
      if (alive.expired()) return;
      pulling_ = false;
      paste_->setText("Paste");
      pasteNow();
      update();
    });
  }
  void pasteNow() {
    if (!field_->paste()) ctx().toast(clipboard::empty() ? "Clipboard is empty" : "The message is full");
  }

  static void setEnabled(ui::Button* b, bool on) {
    if (b->enabled == on) return;
    b->enabled = on;
    b->invalidate(Hint::Content);
  }

  // Always asked: who it goes to and the text, then Send or Cancel.
  void confirm() {
    if (busy_ || trimmed(field_->text()).empty()) return;
    auto sheet = std::make_unique<ui::Sheet>();
    ui::Sheet* raw = sheet.get();
    ui::Column* c = sheet->content();
    c->add<ui::Label>("Send to " + to_.name + "?", Type::Title)->maxLines = 2;
    auto* text = c->add<ui::Label>(trimmed(field_->text()), Type::Body);  // the message itself: ink
    text->maxLines = 7;
    text->keepLineBreaks = true;  // a score is lines of squares
    c->add<ui::Label>("Your phone sends it from your WhatsApp account", Type::Caption, ui::secondary);
    c->add<ui::Button>("Send", [this, raw] {
      raw->close();
      send();
    });
    c->add<ui::Button>("Cancel", [raw] { raw->close(); }, ui::Button::Kind::Secondary);
    field_->host()->addOverlay(std::move(sheet));
  }

  void send() {
    if (busy_) return;
    companion::Pairing pairing;
    if (!companion::loadPairing(pairing)) {
      ctx().toast("No phone paired: see Settings > Home Assistant");
      return;
    }
    // The counter is stored before the push (COMPANION.md 5): a lost answer never makes the Paper reuse it.
    const std::string body = companion::replyEnvelope(pairing, to_.key, trimmed(field_->text()), ctx().clock.now());
    if (body.empty() || !companion::savePairing(pairing)) {
      ctx().toast("Not sent: the message does not fit");
      return;
    }
    busy_ = true;
    send_->setText("Sending...");
    update();
    HaClient ha = ctx().ha();
    const uint64_t ref = pairing.tx;
    const std::string name = to_.name;
    ctx().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); },
                [this, ref, name](HaResult r) {
                  busy_ = false;
                  send_->setText("Send");
                  const SendOutcome o = pushOutcome(r);
                  ctx().log("reply: %s", o.sent ? "pushed" : "not pushed");  // never the text
                  ctx().toast(o.message);
                  if (o.sent) {
                    companion::Notifs n;
                    companion::loadNotifs(n);
                    n.sent.push_back(companion::SentReply{ref, name});
                    if (n.sent.size() > 10) n.sent.erase(n.sent.begin());
                    companion::saveNotifs(n);
                    ctx().nav.pop();
                    return;
                  }
                  update();  // the text stays
                });
  }

  Target to_;
  std::string initial_;
  bool busy_ = false;
  bool pulling_ = false;
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
  ui::TextField* field_ = nullptr;
  ui::Label* counter_ = nullptr;
  ui::Button *paste_ = nullptr, *send_ = nullptr;
  ui::Widget* panel_ = nullptr;
};

}  // namespace

bool openCompose(AppContext& ctx, const Target& to, const std::string& text) {
  if (to.key.empty()) {
    ctx.toast("Not a WhatsApp chat");
    return false;
  }
  ctx.nav.push<ComposeScreen>(to, text);
  return true;
}

bool sendToFavorite(AppContext& ctx, const std::string& name, const std::string& text) {
  companion::Pairing pairing;
  if (!companion::loadPairing(pairing)) {
    ctx.toast("No phone paired: see Settings > Home Assistant");
    return false;
  }
  companion::Notifs n;
  companion::loadNotifs(n);
  Target t;
  if (!findByName(n, name, t)) {
    ctx.toast("No WhatsApp notification from " + name + " on your phone");
    return false;
  }
  return openCompose(ctx, t, text);
}

}  // namespace pos::whatsapp
