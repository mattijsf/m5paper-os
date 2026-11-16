// WhatsApp and Mail: one implementation, registered twice (docs/apps/inbox.md). The phone's notifications, decrypted from the
// Home Assistant relay (docs/COMPANION.md, relay_fetch.h), as a list: chat conversations, mails (sender, subject, the preview the
// notification showed), "Mark all read". WhatsApp can reply to a chat whose notification is still on the phone (compose.h).
#include <algorithm>
#include <cstdio>
#include <memory>

#include <ArduinoJson.h>

#include "apps/inbox/compose.h"
#include "apps/inbox/inbox_model.h"
#include "apps/inbox/relay_fetch.h"
#include "core/companion.h"
#include "core/app.h"
#include "core/fs.h"
#include "core/os.h"
#include "core/text_clean.h"
#include "core/timefmt.h"
#include "ui/root.h"
#include "ui/widgets.h"

namespace pos::inbox {

namespace {

using ui::Type;

struct Kind {
  const char* id;
  const char* name;
  Icon icon;
  const char* entity;
  bool mail;
};
constexpr Kind kWhatsApp = {"whatsapp", "WhatsApp", Icon::MessageCircle, companion::kToPaperEntity, false};
constexpr Kind kMail = {"mail", "Mail", Icon::Mail, companion::kToPaperEntity, true};

constexpr int kRowsPerPage = 6;
constexpr uint32_t kStaleMs = 60 * 1000;

// Splits cleaned text into paragraphs (one per non-empty line) for a TextBlock.
std::vector<ui::Paragraph> paragraphsOf(const std::string& text, bool boldFirst = false) {
  std::vector<ui::Paragraph> out;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    const std::string line = text.substr(pos, e - pos);
    pos = e + 1;
    if (line.empty()) {
      if (e >= text.size()) break;
      continue;
    }
    ui::Paragraph p;
    p.runs.push_back(ui::Run{line, boldFirst && out.empty(), false});
    out.push_back(std::move(p));
    if (e >= text.size()) break;
  }
  return out;
}

ui::TextStyle detailStyle() {
  ui::TextStyle s;
  s.body = Type::ReadS;
  return s;
}

// A conversation (chat) or one mail as text pages. Tap right = next page, left third = back.
class DetailScreen : public Screen {
 public:
  DetailScreen(const Kind& kind, Msg msg, std::vector<Msg> thread, int64_t now) : kind_(kind), msg_(std::move(msg)), thread_(std::move(thread)), now_(now) {}

  void build(ui::Column& root) override {
    if (kind_.mail) {
      setTitle("Mail");
      root.add<ui::Spacer>(ui::s1);
      root.add<ui::Label>(msg_.from, Type::Body);  // the sender is content: ink, body size
      auto* subject = root.add<ui::Label>(msg_.subject.empty() ? "(no subject)" : msg_.subject, Type::Title);
      subject->maxLines = 2;
      root.add<ui::Label>(relativeTime(msg_.ts, now_), Type::Caption, ui::secondary);
      root.add<ui::Label>("The preview your phone showed", Type::Caption, ui::secondary);
      root.add<ui::Spacer>(ui::s1);
    } else {
      setTitle(msg_.chat.empty() ? msg_.from : msg_.chat);
      root.add<ui::Spacer>(ui::s1);
    }
    block_ = root.add<ui::TextBlock>();
    block_->flex = 1;
    block_->setStyle(detailStyle());
    pageLabel_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    pageLabel_->align = ui::TextAlign::Center;
    root.add<ui::Spacer>(ui::s1);
    if (!kind_.mail && msg_.canReply) {  // only while the notification (with its reply action) is on the phone
      const whatsapp::Target to{msg_.chat.empty() ? msg_.from : msg_.chat, msg_.key};
      root.add<ui::Button>("Reply", [this, to] { whatsapp::openCompose(ctx(), to); }, ui::Button::Kind::Secondary);
      root.add<ui::Spacer>(ui::s1);
    }
    block_->onPageChange = [this](int) { updatePage(); };
    block_->onLayout = [this] { updatePage(); };
    if (kind_.mail) {
      block_->setParagraphs(paragraphsOf(msg_.text.empty() ? "(no preview)" : msg_.text));
    } else {
      block_->setParagraphs(chatParagraphs());
    }
  }

 private:
  void updatePage() {
    const int n = block_->pageCount();
    pageLabel_->setText(n > 1 ? "Page " + std::to_string(block_->pageIndex() + 1) + " of " + std::to_string(n) : "");
  }

  // The conversation, newest first: sender (in a group) and time, then the text.
  std::vector<ui::Paragraph> chatParagraphs() const {
    std::vector<ui::Paragraph> out;
    for (auto it = thread_.begin(); it != thread_.end(); ++it) {
      ui::Paragraph head;
      head.runs.push_back(ui::Run{(msg_.chat.empty() ? "" : it->from + "  ") + relativeTime(it->ts, now_), true, false});
      out.push_back(std::move(head));
      for (auto& p : paragraphsOf(it->text)) out.push_back(std::move(p));
    }
    return out;
  }

  const Kind& kind_;
  Msg msg_;
  std::vector<Msg> thread_;
  int64_t now_;
  ui::TextBlock* block_ = nullptr;
  ui::Label* pageLabel_ = nullptr;
};

class ListScreen : public Screen {
 public:
  explicit ListScreen(const Kind& kind) : kind_(kind) {}

  void build(ui::Column& root) override {
    setTitle(kind_.name);
    setAction(Icon::RefreshCw, [this] { refresh(true); });
    seenAtStart_ = ctx().store.get("seen", 0);
    holder_ = root.add<ui::Column>(0);
    holder_->flex = 1;
    status_ = root.add<ui::Label>("", Type::Caption, ui::secondary);
    status_->align = ui::TextAlign::Center;
    markAll_ = root.add<ui::Button>("Mark all read", ui::Button::Kind::Secondary);
    markAll_->onClick = [this] { confirmMarkAll(); };
    root.add<ui::Spacer>(ui::s1);
    companion::Pairing pairing;
    notPaired_ = !companion::loadPairing(pairing);
    companion::Notifs saved;
    if (!notPaired_ && companion::loadNotifs(saved)) {
      feed_ = feedFromNotifs(saved, kind_.mail);
      cleanFeed(feed_, kind_.mail);
      haveData_ = true;
    }
    rebuild();
  }

  void onShow() override {
    // After a detail screen: read again only when the data is older than a minute.
    if (!fetchedOnce_ || ctx().clock.millis() - lastFetchMs_ > kStaleMs) refresh(false);
  }

  std::string debugState() const override {
    return std::string("rows=") + std::to_string(rowCount_) + "|" + status_->text() + (error_ ? "|error" : "") +
           (notSetUp_ ? "|notsetup" : "") + (notPaired_ ? "|notpaired" : "");
  }

 private:
  int64_t now() { return ctx().clock.now(); }

  void rebuild() {
    while (!holder_->children().empty()) holder_->remove(holder_->children().back().get());
    rows_.clear();
    pager_ = nullptr;
    convs_.clear();
    rowCount_ = 0;
    if (notPaired_) {
      holder_->add<ui::EmptyState>(kind_.icon, "No phone paired",
                                   "Messages come from the PaperOS app on your phone. Pair it in Settings > Home Assistant > Phone.")->flex = 1;
      markAll_->visible = false;
      holder_->host()->relayout();
      return;
    }
    if (notSetUp_) {  // the relay does not exist in Home Assistant (404)
      holder_->add<ui::EmptyState>(kind_.icon, "Not set up yet",
                                   "The relay for your phone's messages is missing in Home Assistant.")->flex = 1;
      markAll_->visible = false;
      holder_->host()->relayout();
      return;
    }
    if (error_ && !haveData_) {
      holder_->add<ui::EmptyState>(Icon::WifiOff, "Cannot reach Home Assistant", "Check Wi-Fi and the address, then try again.", "Retry", [this] { refresh(true); })->flex = 1;
      markAll_->visible = false;
      holder_->host()->relayout();
      return;
    }
    // Rows: one per conversation (chat) or per mail.
    if (kind_.mail) {
      for (size_t i = 0; i < feed_.items.size(); ++i) {
        Conversation c;
        c.key = feed_.items[i].key;
        c.title = feed_.items[i].from;
        c.lastText = feed_.items[i].subject.empty() ? feed_.items[i].text : feed_.items[i].subject;
        c.ts = feed_.items[i].ts;
        c.count = 1;
        c.isNew = feed_.items[i].ts > seenAtStart_;
        c.items = {static_cast<int>(i)};
        convs_.push_back(std::move(c));
      }
    } else {
      convs_ = groupChats(feed_.items, seenAtStart_);
    }
    rowCount_ = static_cast<int>(convs_.size());
    markAll_->visible = rowCount_ > 0;
    if (convs_.empty()) {
      holder_->add<ui::EmptyState>(kind_.icon, "No new messages", "")->flex = 1;
      holder_->host()->relayout();
      return;
    }
    const int pages = std::max(1, (rowCount_ + kRowsPerPage - 1) / kRowsPerPage);
    pager_ = holder_->add<ui::Pager>(pages);
    pager_->flex = 1;
    pager_->bar()->visible = pages > 1;
    auto* list = pager_->add<ui::Column>(0);
    for (int i = 0; i < kRowsPerPage; ++i) rows_.push_back(list->add<ui::ListItem>());
    pager_->onPage = [this](int p) { fill(p); };
    fill(0);
    holder_->host()->relayout();
  }

  void fill(int page) {
    for (int i = 0; i < kRowsPerPage; ++i) {
      const int idx = page * kRowsPerPage + i;
      ui::ListItem* row = rows_[i];
      row->visible = idx < rowCount_;
      if (idx >= rowCount_) continue;
      const Conversation& c = convs_[idx];
      std::string title = (c.isNew ? "\xE2\x80\xA2 " : "") + c.title;
      if (c.count > 1) title += " (" + std::to_string(c.count) + ")";
      row->setTitle(title);
      row->setSubtitle(firstLine(c.lastText));
      row->setSubtitleIsContent(true);  // the message (or the mail subject) is what the list is for
      row->setTrailingText(relativeTime(c.ts, now()));
      row->onClick = [this, idx] { open(idx); };
    }
    if (rows_.empty() == false) {
      auto* parent = rows_[0]->parent();
      if (parent && parent->frame.w > 0) parent->layout();
    }
  }

  void open(int idx) {
    const Conversation& c = convs_[idx];
    std::vector<Msg> thread;
    for (int i : c.items) thread.push_back(feed_.items[i]);
    ctx().nav.push<DetailScreen>(kind_, feed_.items[c.items[0]], std::move(thread), now());
  }

  void refresh(bool announce) {
    if (busy_) return;
    companion::Pairing pairing;
    notPaired_ = !companion::loadPairing(pairing);
    if (notPaired_) {
      rebuild();
      return;
    }
    busy_ = true;
    if (!haveData_) status_->setText("Loading...");
    HaClient ha = ctx().ha();
    const int64_t t = now();
    const bool clip = ctx().config.phone.shareClipboard;
    ctx().async([ha, t, clip]() mutable { return fetchRelay(ha, t, clip); }, [this, announce](RelayFetch f) {
      busy_ = false;
      fetchedOnce_ = true;
      lastFetchMs_ = ctx().clock.millis();
      notPaired_ = !f.paired;
      notSetUp_ = f.http.error == HaError::NotFound;
      if (notPaired_ || notSetUp_) {
        feed_.items.clear();
        haveData_ = false;
        error_ = false;
        writeBadge(0);
        status_->setText("");
        if (notPaired_ && f.applied.unpaired) ctx().toast("Your phone ended the pairing");
        rebuild();
        return;
      }
      if (f.http.ok() && f.applied.parsed) {
        feed_ = feedFromNotifs(f.notifs, kind_.mail);
        cleanFeed(feed_, kind_.mail);
        haveData_ = true;
        error_ = false;
        status_->setText("Updated " + clockText());
        markSeen();
        rebuild();
        if (!f.applied.notes.empty()) ctx().toast(f.applied.notes.back());  // the phone's answer to a reply
        else if (f.clipFromPhone) ctx().toast("Clipboard from your phone");
        return;
      }
      error_ = true;
      if (haveData_) {
        ctx().toast(f.http.ok() ? "Unexpected answer from Home Assistant" : haErrorText(f.http.error));  // the saved list stays
        status_->setText("Saved data");
      } else {
        status_->setText("");
        rebuild();
      }
      (void)announce;
    });
  }

  std::string clockText() {
    const int64_t t = now();
    if (t <= 0) return "just now";
    struct tm tmv;
    localTime(t, tmv);
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    return buf;
  }

  // The seen marker is the newest message shown; the launcher badge is what is newer than it (nothing, right after a look).
  void markSeen() {
    const int64_t newest = newestTs(feed_.items);
    if (newest > ctx().store.get("seen", 0)) ctx().store.set("seen", static_cast<int>(newest));
    writeBadge(newCount(feed_.items, ctx().store.get("seen", 0)));
  }

  void writeBadge(int n) { ctx().cache.write("badge", std::to_string(n)); }

  void confirmMarkAll() {
    auto sheet = std::make_unique<ui::Sheet>();
    ui::Column* c = sheet->content();
    ui::Sheet* raw = sheet.get();
    c->add<ui::Label>("Mark all read?", Type::Title);
    c->add<ui::Label>("The list is cleared here. The notifications stay on your phone.", Type::Body, ui::secondary)->maxLines = 2;
    c->add<ui::Button>("Mark all read", [this, raw] {
      raw->close();
      markAllRead();
    });
    c->add<ui::Button>("Cancel", [raw] { raw->close(); }, ui::Button::Kind::Secondary);
    holder_->host()->addOverlay(std::move(sheet));
  }

  // Clears this app's notifications on the Paper and tells the phone (`dismiss`, best effort: COMPANION.md 7.2).
  void markAllRead() {
    companion::Notifs n;
    companion::loadNotifs(n);
    std::vector<std::string> keys;
    const std::string app = kind_.mail ? "mail" : "whatsapp";
    for (const companion::Notif& x : n.items)
      if (x.app == app) keys.push_back(x.key);
    n.items.erase(std::remove_if(n.items.begin(), n.items.end(), [&](const companion::Notif& x) { return x.app == app; }), n.items.end());
    companion::saveNotifs(n);
    feed_.items.clear();
    writeBadge(0);
    rebuild();
    ctx().toast("All read");
    companion::Pairing pairing;
    if (keys.empty() || !companion::loadPairing(pairing)) return;
    const std::string body = companion::dismissEnvelope(pairing, keys);
    if (body.empty() || !companion::savePairing(pairing)) return;
    HaClient ha = ctx().ha();
    ctx().async([ha, body]() mutable { return ha.callService("script", "paperos_relay_push", body); }, [](HaResult) {});
  }

  const Kind& kind_;
  Feed feed_;
  std::vector<Conversation> convs_;
  int rowCount_ = 0;
  int64_t seenAtStart_ = 0;
  bool haveData_ = false, busy_ = false, error_ = false, notSetUp_ = false, notPaired_ = false, fetchedOnce_ = false;
  uint32_t lastFetchMs_ = 0;
  ui::Column* holder_ = nullptr;
  ui::Pager* pager_ = nullptr;
  ui::Label* status_ = nullptr;
  ui::Button* markAll_ = nullptr;
  std::vector<ui::ListItem*> rows_;
};

template <const Kind& K>
class InboxApp : public App {
 public:
  void onStart(AppContext& ctx) override { ctx.nav.push<ListScreen>(K); }
};

using WhatsAppApp = InboxApp<kWhatsApp>;
using MailApp = InboxApp<kMail>;

POS_REGISTER_APP(WhatsAppApp, {.id = "whatsapp", .name = "WhatsApp", .icon = Icon::MessageCircle, .order = 30});
POS_REGISTER_APP(MailApp, {.id = "mail", .name = "Mail", .icon = Icon::Mail, .order = 40});

}  // namespace

}  // namespace pos::inbox
