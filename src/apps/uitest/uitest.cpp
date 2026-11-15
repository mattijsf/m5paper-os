#include "apps/uitest/uitest.h"

#include "ui/label.h"
#include "ui/layout.h"

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

}  // namespace

std::unique_ptr<Screen> create(const std::string& name, Canvas& canvas, DisplayManager& dm) {
  std::unique_ptr<Widget> root;
  if (name == "widgets1") root = widgets1();
  if (!root) return nullptr;
  auto screen = std::make_unique<Screen>(canvas, dm);
  screen->setRoot(std::move(root));
  return screen;
}

}  // namespace pos::apps::uitest
