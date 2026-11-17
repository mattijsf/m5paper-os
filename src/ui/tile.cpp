#include "ui/tile.h"

namespace pos::ui {

void Tile::setLabel(const std::string& t) { if (t != label_) { label_ = t; invalidate(Hint::Content); } }
void Tile::setValue(const std::string& t) { if (t != value_) { value_ = t; invalidate(Hint::Content); } }
void Tile::setBadge(int n) { if (n != badge_) { badge_ = n; invalidate(Hint::Content); } }
void Tile::setIcon(Icon i) { if (i != icon_) { icon_ = i; invalidate(Hint::Content); } }

void Tile::setBlank(bool b) {
  if (b == blank_) return;
  blank_ = b;
  touchable = !b;
  invalidate(Hint::Content);
}

void Tile::draw(Canvas& c) {
  if (blank_) return;
  strokeRound(c, frame, kRadius, pressed_ ? 4 : kStroke, ink);
  const int cx = frame.x + frame.w / 2;
  if (size_ == Size::Small) {
    // The same icon and well as the large tile (a smaller well shows the corners of the icon's own square), the label
    // in the body size (it has 137 px: "WhatsApp" fits).
    const int iconY = frame.y + 62;
    c.fillCircle(cx, iconY, 44, fill);
    drawIcon(c, icon_, cx, iconY, 64, ink, fill);
    drawText(c, ellipsize(c, label_, Type::BodyStrong, frame.w - 2 * s1), cx, frame.y + 132, Type::BodyStrong, ink, Anchor::MiddleCenter, bg());
    if (badge_ > 0) drawBadge(c, frame.x + frame.w - s1 - 4, frame.y + s1 + 18, badge_, bg());
    return;
  }
  const bool hasValue = !value_.empty();
  const int iconY = frame.y + (hasValue ? 60 : 72);
  c.fillCircle(cx, iconY, 44, fill);
  drawIcon(c, icon_, cx, iconY, 64, ink, fill);
  const int maxW = frame.w - 2 * s2;
  drawText(c, ellipsize(c, label_, Type::Title, maxW), cx, frame.y + (hasValue ? 128 : 150), Type::Title, ink, Anchor::MiddleCenter, bg());
  if (hasValue)
    drawText(c, ellipsize(c, value_, Type::Body, maxW), cx, frame.y + 164, Type::Body, ink, Anchor::MiddleCenter, bg());  // the value is the content
  if (badge_ > 0) drawBadge(c, frame.x + frame.w - s2, frame.y + s2 + 14, badge_, bg());
}

}  // namespace pos::ui
