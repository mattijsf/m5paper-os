// A QR code (byte mode, error correction M), black modules on white with a four-module quiet zone, each module a whole number of
// pixels so the e-ink panel draws it sharp. Used by Settings > Home Assistant > Phone for pairing (docs/COMPANION.md section 2).
#pragma once

#include <string>
#include <vector>

#include "ui/widget.h"

namespace pos::ui {

class QrCode : public Widget {
 public:
  // `maxPx`: the largest side the code may take; the module size is the biggest that fits.
  QrCode(const std::string& text, int maxPx);
  bool ok() const { return size_ > 0; }
  Size measure(int availW) override;
  void draw(Canvas& c) override;

 private:
  int size_ = 0;  // modules per side, 0 if the text does not fit version 10
  int maxPx_;
  std::vector<uint8_t> dark_;  // size_ * size_
  int modulePx(int avail) const;
};

}  // namespace pos::ui
