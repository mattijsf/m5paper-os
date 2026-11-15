#include "ui/tile.h"

namespace pos::ui {

void Tile::setLabel(const std::string& t) { if (t != label_) { label_ = t; invalidate(Hint::Content); } }
void Tile::setValue(const std::string& t) { if (t != value_) { value_ = t; invalidate(Hint::Content); } }
void Tile::setBadge(int n) { if (n != badge_) { badge_ = n; invalidate(Hint::Content); } }
void Tile::setIcon(Icon i) { if (i != icon_) { icon_ = i; invalidate(Hint::Content); } }

void Tile::draw(Canvas& c) {
  strokeRound(c, frame, kRadius, pressed_ ? 4 : kStroke, ink);
  const int cx = frame.x + frame.w / 2;
  const bool hasValue = !value_.empty();
  const int iconY = frame.y + (hasValue ? 60 : 72);
  c.fillCircle(cx, iconY, 44, fill);
  drawIcon(c, icon_, cx, iconY, 64, ink, fill);
  const int maxW = frame.w - 2 * s2;
  drawText(c, ellipsize(c, label_, Type::Title, maxW), cx, frame.y + (hasValue ? 128 : 150), Type::Title, ink, Anchor::MiddleCenter, bg());
  if (hasValue)
    drawText(c, ellipsize(c, value_, Type::Body, maxW), cx, frame.y + 164, Type::Body, secondary, Anchor::MiddleCenter, bg());
  if (badge_ > 0) drawBadge(c, frame.x + frame.w - s2, frame.y + s2 + 14, badge_, bg());
}

}  // namespace pos::ui
