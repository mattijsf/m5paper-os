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
    // The same 64 px icon, straight on the paper: no gray well. A screen with gray areas needs the Text waveform, which
    // draws in three visible steps (camera, 2026-11-17: Settings to the launcher flickered for 370 ms), and gray(13) reads as almost white on the panel anyway. Ink and paper only: the
    // launcher changes in one Fast pass. The label in the body size (it has 137 px: "WhatsApp" fits).
    const int iconY = frame.y + 62;
    drawIcon(c, icon_, cx, iconY, 64, ink, bg());
    drawText(c, ellipsize(c, label_, Type::BodyStrong, frame.w - 2 * s1), cx, frame.y + 132, Type::BodyStrong, ink, Anchor::MiddleCenter, bg());
    if (badge_ > 0) drawBadge(c, frame.x + frame.w - s1 - 4, frame.y + s1 + 18, badge_, bg());
    return;
  }
  const bool hasValue = !value_.empty();
  const int iconY = frame.y + (hasValue ? 60 : 72);
  drawIcon(c, icon_, cx, iconY, 64, ink, bg());  // (no gray well here either: the same reason as above)
  const int maxW = frame.w - 2 * s2;
  drawText(c, ellipsize(c, label_, Type::Title, maxW), cx, frame.y + (hasValue ? 128 : 150), Type::Title, ink, Anchor::MiddleCenter, bg());
  if (hasValue)
    drawText(c, ellipsize(c, value_, Type::Body, maxW), cx, frame.y + 164, Type::Body, ink, Anchor::MiddleCenter, bg());  // the value is the content
  if (badge_ > 0) drawBadge(c, frame.x + frame.w - s2, frame.y + s2 + 14, badge_, bg());
}

}  // namespace pos::ui
