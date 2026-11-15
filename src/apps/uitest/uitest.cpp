#include "apps/uitest/uitest.h"

#include "ui/controls.h"
#include "ui/inputs.h"
#include "ui/keyboard.h"
#include "ui/panels.h"
#include "ui/label.h"
#include "ui/layout.h"
#include "ui/list.h"
#include "ui/textblock.h"
#include "ui/tile.h"

namespace pos::apps::uitest {

using namespace pos::ui;

namespace {

// widgets1 (P2-1): labels in every type token, wrapping and ellipsis, icons at both sizes.
std::unique_ptr<Widget> widgets1() {
  auto root = std::make_unique<Padding>(kMargin, s2, kMargin, s2);
  auto* col = root->add<Column>(s1);
  col->add<Label>("Widgets 1", Type::Headline);

  struct Row_ {
    Type type;
    const char* text;
  };
  const Row_ rows[] = {
      {Type::Caption, "caption 18: Hamburgefonstiv 0123"}, {Type::Body, "body 22: Hamburgefonstiv 0123"},
      {Type::BodyStrong, "bodyStrong 22: Hamburgefonstiv"}, {Type::Title, "title 28: Hamburgefonstiv"},
      {Type::ReadS, "read-S 24: Hamburgefonstiv 0123"},    {Type::ReadM, "read-M 28: Hamburgefonstiv"},
      {Type::ReadL, "read-L 32: Hamburgefonstiv"},          {Type::ReadXL, "read-XL 38: Hamburgefonstiv"},
      {Type::ReadItalicM, "read-M italic: Hamburgefonstiv"}, {Type::ReadBoldM, "read-M bold: Hamburgefonstiv"},
  };
  for (const Row_& r : rows) col->add<Label>(r.text, r.type);

  auto* wrapped = col->add<Label>(
      "A label can wrap onto several lines at word boundaries, and when maxLines is reached the last line ends "
      "with an ellipsis instead of running off the screen.",
      Type::Body, secondary);
  wrapped->maxLines = 3;

  col->add<Spacer>(s1);
  auto* small = col->add<Row>(s2);
  const Icon small_icons[] = {Icon::House, Icon::BookOpen, Icon::Lightbulb, Icon::Wifi, Icon::Battery,
                              Icon::Settings, Icon::Mail, Icon::Clock};
  for (Icon i : small_icons) small->add<IconView>(i, 32);
  auto* big = col->add<Row>(s2);
  const Icon big_icons[] = {Icon::House, Icon::BookOpen, Icon::Sofa, Icon::Thermometer, Icon::Fan};
  for (Icon i : big_icons) big->add<IconView>(i, 64);
  col->add<Spacer>(0, 1);  // flex: pushes the footer to the bottom
  auto* footer = col->add<Label>("flex spacer above, footer at the bottom", Type::Caption, secondary);
  footer->align = TextAlign::Center;
  return root;
}

// widgets2 (P2-2): controls in normal / pressed / on states. The first Button, the first Toggle
// and the first ListItem of widgets2rows are live (tap them in the UI script).
std::unique_ptr<Widget> widgets2() {
  auto root = std::make_unique<Padding>(kMargin, s2, kMargin, s2);
  auto* col = root->add<Column>(s1);
  col->add<Label>("Widgets 2", Type::Title);

  auto caption = [&](const char* t) { col->add<Label>(t, Type::Caption, secondary); };
  auto buttons = [&](bool pressed) {
    auto* row = col->add<Row>(s2);
    for (auto k : {Button::Kind::Primary, Button::Kind::Secondary, Button::Kind::Text}) {
      const char* t = k == Button::Kind::Primary ? "Save" : k == Button::Kind::Secondary ? "Cancel" : "Skip";
      auto* b = row->add<Button>(t, k);
      b->setPressed(pressed);
      if (!pressed && k == Button::Kind::Primary) b->onClick = [b] { b->setText("Saved"); };
    }
    auto* dis = row->add<Button>("Off", Button::Kind::Secondary);
    dis->enabled = false;
  };
  caption("Buttons: normal, then pressed (last = disabled)");
  buttons(false);
  buttons(true);

  caption("IconButton normal / pressed, Badge");
  auto* row = col->add<Row>(s2);
  row->add<IconButton>(Icon::Settings);
  row->add<IconButton>(Icon::Settings)->setPressed(true);
  row->add<Badge>(3);
  row->add<Badge>(120);
  caption("Toggle: off, on, pressed off, pressed on");
  auto* trow = col->add<Row>(s3);
  trow->add<Toggle>(false);
  trow->add<Toggle>(true);
  trow->add<Toggle>(false)->setPressed(true);
  trow->add<Toggle>(true)->setPressed(true);

  caption("Tile: with value and badge; pressed");
  auto* tiles = col->add<Row>(s3);
  auto* t1 = tiles->add<Tile>(Icon::Thermometer, "Living");
  t1->setValue("21.5 \xC2\xB0" "C");
  t1->setBadge(2);
  t1->flex = 1;
  auto* t2 = tiles->add<Tile>(Icon::Mail, "Mail");
  t2->setPressed(true);
  t2->flex = 1;
  return root;
}

// widgets2rows (P2-2): ListItem variants.
std::unique_ptr<Widget> widgets2rows() {
  auto root = std::make_unique<Padding>(kMargin, s2, kMargin, s2);
  auto* col = root->add<Column>(0);
  col->add<Label>("ListItem", Type::Title);
  col->add<Spacer>(s1);
  auto* a = col->add<ListItem>("Wi-Fi", "Home network", true, Icon::Wifi);
  a->setChevron();
  a->onClick = [a] { a->setSubtitle("Tapped"); };
  col->add<ListItem>("Battery", "", true, Icon::Battery)->setTrailingText("87%");
  auto* night = col->add<ListItem>("Night mode", "", true, Icon::Moon);
  night->setToggle(true);
  col->add<ListItem>("Messages", "Unread", false)->setBadge(4);
  auto* pr = col->add<ListItem>("Pressed row", "second line", true, Icon::Folder);
  pr->setChevron();
  pr->setPressed(true);
  col->add<ListItem>("A very long title that has to be cut with an ellipsis before the chevron", "", true, Icon::FileText)->setChevron();
  return root;
}

// widgets2list (P2-2): a 20-row list in a Pager, 7 rows per page.
std::unique_ptr<Widget> widgets2list() {
  auto root = std::make_unique<Padding>(kMargin, 0, kMargin, 0);
  auto* col = root->add<Column>(0);
  col->add<Spacer>(s1);
  col->add<Label>("Pager: 20 rows, 7 per page", Type::Title);
  col->add<Spacer>(s1);
  auto* pager = col->add<Pager>(3);
  pager->flex = 1;
  auto* list = pager->add<Column>(0);
  constexpr int kRows = 20, kPerPage = 7;
  std::vector<ListItem*> items;
  for (int i = 0; i < kPerPage; ++i) items.push_back(list->add<ListItem>("", "", true, Icon::FileText));
  auto fill = [items, pager](int page) {
    for (int i = 0; i < kPerPage; ++i) {
      const int n = page * kPerPage + i;
      items[i]->visible = n < kRows;
      if (n < kRows) {
        items[i]->setTitle("Item " + std::to_string(n + 1));
        items[i]->setSubtitle(n % 2 ? "" : "Every other row has a subtitle");
        items[i]->setChevron();
      }
    }
    (void)pager;
  };
  fill(0);
  pager->onPage = fill;
  return root;
}

// widgets3 (P2-3): Segmented, Stepper, LevelBar, EmptyState, Toast and Sheet. Every control is live.
std::unique_ptr<Widget> widgets3() {
  auto root = std::make_unique<Padding>(kMargin, s2, kMargin, s2);
  auto* col = root->add<Column>(s2);
  col->add<Label>("Widgets 3", Type::Title);
  auto caption = [&](const char* t) { col->add<Label>(t, Type::Caption, secondary); };

  caption("Segmented");
  col->add<Segmented>(std::vector<std::string>{"Day", "Night", "Auto"}, 0);
  caption("Stepper (target temperature)");
  col->add<Stepper>(21, 15, 25, 1, [](int v) { return std::to_string(v) + " \xC2\xB0" "C"; });
  caption("LevelBar (brightness)");
  col->add<LevelBar>(60);

  auto* row = col->add<Row>(s2);
  auto* toastBtn = row->add<Button>("Toast", Button::Kind::Secondary);
  auto* sheetBtn = row->add<Button>("Sheet", Button::Kind::Primary);
  toastBtn->onClick = [toastBtn] { toastBtn->host()->showToast("No Wi-Fi: showing saved data"); };
  sheetBtn->onClick = [sheetBtn] {
    auto sheet = std::make_unique<Sheet>();
    Column* c = sheet->content();
    c->add<Label>("Quick controls", Type::Title);
    c->add<Segmented>(std::vector<std::string>{"Off", "Low", "High"}, 1);
    c->add<LevelBar>(30);
    auto* done = c->add<Button>("Done", Button::Kind::Primary);
    Sheet* raw = sheet.get();
    done->onClick = [raw] { raw->close(); };
    sheetBtn->host()->addOverlay(std::move(sheet));
  };

  auto* empty = col->add<EmptyState>(Icon::WifiOff, "No connection", "Connect to Wi-Fi to load messages.", "Retry");
  empty->flex = 1;
  Button* retry = empty->button();
  retry->onClick = [retry] { retry->host()->showToast("Still offline"); };
  return root;
}

// widgets4 (P2-7): a password field on top, the keyboard at the bottom (full width).
std::unique_ptr<Widget> widgets4() {
  auto root = std::make_unique<Column>(0);
  auto* pad = root->add<Padding>(kMargin, s2, kMargin, s2);
  auto* col = pad->add<Column>(s2);
  col->add<Label>("Wi-Fi password", Type::Title);
  auto* field = col->add<TextField>("Password");
  field->setMasked(true);
  auto* shown = col->add<Label>("", Type::Caption, secondary);
  pad->flex = 1;  // the padded block takes the rest: the keyboard sits at the bottom
  auto* kb = root->add<Keyboard>();
  // The field is masked: the caption shows only how many characters were typed.
  bindKeyboard(*kb, *field, [shown, field] { shown->setText("Entered " + std::to_string(field->text().size()) + " characters"); });
  auto inner = kb->onKey;
  kb->onKey = [inner, shown, field](Keyboard::Kind k, const std::string& t) {
    inner(k, t);
    if (k != Keyboard::Kind::Enter) shown->setText(std::to_string(field->text().size()) + " characters");
  };
  return root;
}

// widgets5 (P2-7): TextBlock with headings, bold and italic runs; tap right = next page, left third = back.
std::unique_ptr<Widget> widgets5() {
  auto root = std::make_unique<Padding>(kMargin, s2, kMargin, s2);
  auto* col = root->add<Column>(s1);
  auto* info = col->add<Label>("", Type::Caption, secondary);
  auto* tb = col->add<TextBlock>();
  tb->flex = 1;
  std::vector<Paragraph> paras;
  auto heading = [&](const char* t, int level) {
    Paragraph p;
    p.heading = level;
    p.runs.push_back(Run{t, false, false});
    paras.push_back(p);
  };
  auto body = [&](std::vector<Run> runs) {
    Paragraph p;
    p.runs = std::move(runs);
    paras.push_back(p);
  };
  heading("The Paper Almanac", 1);
  body({{"An e-ink page does not glow. It reflects the light of the room, the way a printed page does, and it ", false, false},
        {"keeps its picture", true, false}, {" without any power at all. That is why a reader like this one can last for weeks ", false, false},
        {"on a single charge", false, true}, {".", false, false}});
  body({{"Pages are laid out one at a time. A line is filled with as many words as fit the column, then the next line starts; "
         "a paragraph ends with a little extra space, and a heading is never left alone at the bottom of a page.", false, false}});
  heading("Chapter one: light", 2);
  for (int i = 0; i < 6; ++i)
    body({{"Paragraph " + std::to_string(i + 1) + ". Words are measured with the real font, so what is counted here is what is drawn. ", false, false},
          {"Italic runs", false, true}, {" and ", false, false}, {"bold runs", true, false},
          {" change the face in the middle of a line without breaking it, and a long word such as extraordinarily is simply carried to the next line when it does not fit.", false, false}});
  heading("Chapter two: ink", 2);
  for (int i = 0; i < 6; ++i)
    body({{"Second chapter, paragraph " + std::to_string(i + 1) + ". Reading position is stored as a paragraph and a byte offset, not as a page number, "
           "so changing the font size never loses the place.", false, false}});
  tb->setParagraphs(std::move(paras));
  auto update = [info, tb](int) { info->setText("Page " + std::to_string(tb->pageIndex() + 1) + " of " + std::to_string(tb->pageCount())); };
  tb->onPageChange = update;
  tb->onLayout = [update] { update(0); };
  return root;
}

}  // namespace

std::unique_ptr<Root> create(const std::string& name, Os& os) {
  std::unique_ptr<Widget> root;
  if (name == "widgets1") root = widgets1();
  if (name == "widgets2") root = widgets2();
  if (name == "widgets2rows") root = widgets2rows();
  if (name == "widgets3") root = widgets3();
  if (name == "widgets4") root = widgets4();
  if (name == "widgets5") root = widgets5();
  if (name == "widgets2list") root = widgets2list();
  if (!root) return nullptr;
  auto screen = std::make_unique<Root>(os.canvas(), os.displayManager(), &os.timers());
  screen->setRoot(std::move(root));
  return screen;
}

}  // namespace pos::apps::uitest
