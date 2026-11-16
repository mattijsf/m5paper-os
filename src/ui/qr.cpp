#include "ui/qr.h"

#include <algorithm>

#include "lgfx/utility/lgfx_qrcode.h"  // the QR encoder bundled with M5GFX (MIT, ricmoo/QRCode): plain C, no platform code

namespace pos::ui {

namespace {
constexpr int kQuiet = 4;
}

QrCode::QrCode(const std::string& text, int maxPx) : maxPx_(maxPx) {
  for (uint8_t version = 3; version <= 10; ++version) {
    std::vector<uint8_t> buf(lgfx_qrcode_getBufferSize(version));
    QRCode qr;
    if (lgfx_qrcode_initBytes(&qr, buf.data(), version, ECC_MEDIUM, reinterpret_cast<uint8_t*>(const_cast<char*>(text.data())),
                              static_cast<uint16_t>(text.size())) != 0)
      continue;
    size_ = qr.size;
    dark_.assign(size_ * size_, 0);
    for (int y = 0; y < size_; ++y)
      for (int x = 0; x < size_; ++x) dark_[y * size_ + x] = lgfx_qrcode_getModule(&qr, x, y) ? 1 : 0;
    break;
  }
}

int QrCode::modulePx(int avail) const {
  if (size_ == 0) return 0;
  return std::max(1, std::min(avail, maxPx_) / (size_ + 2 * kQuiet));
}

Size QrCode::measure(int availW) {
  const int side = modulePx(availW) * (size_ + 2 * kQuiet);
  return {side, side};
}

void QrCode::draw(Canvas& c) {
  if (size_ == 0) return;
  const int px = modulePx(std::min<int>(frame.w, frame.h));
  const int side = px * (size_ + 2 * kQuiet);
  const int x0 = frame.x + (frame.w - side) / 2, y0 = frame.y + (frame.h - side) / 2;
  c.fillRect(x0, y0, side, side, kWhite);
  for (int y = 0; y < size_; ++y)
    for (int x = 0; x < size_; ++x)
      if (dark_[y * size_ + x]) c.fillRect(x0 + (kQuiet + x) * px, y0 + (kQuiet + y) * px, px, px, kBlack);
}

}  // namespace pos::ui
