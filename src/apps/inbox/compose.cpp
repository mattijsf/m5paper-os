// WhatsApp compose and the favourites list (compose.h). The send itself is one call of script.paperos_whatsapp_send (whatsapp_send.h).
#include "apps/inbox/compose.h"

#include <memory>

#include "core/clipboard.h"
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
    setTitle("Message");
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
    paste_ = buttons->add<ui::Button>("Paste", [this] {
      if (!field_->paste()) ctx().toast(clipboard::empty() ? "Clipboard is empty" : "The message is full");
    }, ui::Button::Kind::Secondary);
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
    setEnabled(paste_, !busy_ && !clipboard::empty());
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
    text->maxLines = 6;
    c->add<ui::Label>("From your own WhatsApp account", Type::Caption, ui::secondary);
    c->add<ui::Button>("Send", [this, raw] {
      raw->close();
      send();
    });
    c->add<ui::Button>("Cancel", [raw] { raw->close(); }, ui::Button::Kind::Secondary);
    field_->host()->addOverlay(std::move(sheet));
  }

  void send() {
    if (busy_) return;
    busy_ = true;
    send_->setText("Sending...");
    update();
    HaClient ha = ctx().ha();
    const std::string body = sendBody(to_, field_->text());
    const std::string name = to_.name;
    ctx().async([ha, body]() mutable { return ha.callService("script", "paperos_whatsapp_send", body, true); },
                [this, name](HaResult r) {
                  busy_ = false;
                  send_->setText("Send");
                  const SendOutcome o = sendOutcome(r, name);
                  ctx().log("send: %s%s", o.sent ? "sent" : "not sent", o.reason.empty() ? "" : (" (" + o.reason + ")").c_str());  // never the text
                  ctx().toast(o.message);
                  if (o.sent) {
                    ctx().nav.pop();
                    return;
                  }
                  update();  // the text stays
                });
  }

  Target to_;
  std::string initial_;
  bool busy_ = false;
  ui::TextField* field_ = nullptr;
  ui::Label* counter_ = nullptr;
  ui::Button *paste_ = nullptr, *send_ = nullptr;
  ui::Widget* panel_ = nullptr;
};

class FavoritesScreen : public Screen {
 public:
  explicit FavoritesScreen(std::string text) : text_(std::move(text)) {}

  void build(ui::Column& root) override {
    setTitle("New message");
    root.add<ui::Spacer>(ui::s1);
    root.add<ui::Label>("Send to", Type::Caption, ui::secondary);
    for (const Target& t : favorites(ctx().config)) {
      auto* row = root.add<ui::ListItem>(t.name, isGroup(t.chat) ? "Group" : "Person");
      row->setChevron();
      row->onClick = [this, t] { openCompose(ctx(), t, text_); };
    }
  }

 private:
  std::string text_;
};

}  // namespace

bool openCompose(AppContext& ctx, const Target& to, const std::string& text) {
  if (!validChat(to.chat)) {
    ctx.toast("Not a WhatsApp chat");
    return false;
  }
  ctx.nav.push<ComposeScreen>(to, text);
  return true;
}

void openNewMessage(AppContext& ctx, const std::string& text) {
  const std::vector<Target> favs = favorites(ctx.config);
  if (favs.empty()) {
    ctx.toast("No favourite chats in the config");
    return;
  }
  if (favs.size() == 1) {
    openCompose(ctx, favs[0], text);
    return;
  }
  ctx.nav.push<FavoritesScreen>(text);
}

bool sendToFavorite(AppContext& ctx, const std::string& name, const std::string& text) {
  for (const Target& t : favorites(ctx.config))
    if (t.name == name) return openCompose(ctx, t, text);
  openNewMessage(ctx, text);
  return false;
}

}  // namespace pos::whatsapp
