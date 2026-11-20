#include "ui/image.h"

#include <algorithm>
#include <cstdint>

#include "core/fs.h"

namespace pos::ui {

// Size of a JPEG (from its SOF marker) or PNG (from IHDR); false if it is neither.
static bool imageSize(const uint8_t* p, size_t n, bool& png, int& w, int& h) {
  png = n > 24 && p[0] == 0x89 && p[1] == 'P' && p[2] == 'N' && p[3] == 'G';
  if (png) {
    w = static_cast<int>((p[16] << 24) | (p[17] << 16) | (p[18] << 8) | p[19]);
    h = static_cast<int>((p[20] << 24) | (p[21] << 16) | (p[22] << 8) | p[23]);
    return w > 0 && h > 0;
  }
  if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return false;
  size_t i = 2;
  while (i + 9 < n) {
    if (p[i] != 0xFF) {
      ++i;
      continue;
    }
    const uint8_t m = p[i + 1];
    if (m == 0xFF) {
      ++i;
      continue;
    }
    if (m == 0xD8 || m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
      i += 2;
      continue;
    }
    const size_t len = static_cast<size_t>(p[i + 2] << 8 | p[i + 3]);
    if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
      h = p[i + 5] << 8 | p[i + 6];
      w = p[i + 7] << 8 | p[i + 8];
      return w > 0 && h > 0;
    }
    i += 2 + len;
  }
  return false;
}

// Decodes a JPEG or PNG and scales it (box filter) to fit a boxW x boxH gray canvas, centred on white. nullptr if it cannot
// be read. The decode goes to a canvas of its own: JPEGs are decoded at 1/2, 1/4 or 1/8 size when they are big.
std::shared_ptr<Canvas> loadImageFit(const std::string& virtualPath, int boxW, int boxH) {
  std::string data;
  if (!fs::readFile(virtualPath, data)) return nullptr;
  return loadImageFitData(data, boxW, boxH);
}

std::shared_ptr<Canvas> loadImageFitData(const std::string& data, int boxW, int boxH) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(data.data());
  bool png;
  int w, h;
  if (!imageSize(p, data.size(), png, w, h) || static_cast<int64_t>(w) * h > 12000000) return nullptr;
  int div = 1;
  if (!png)
    while (div < 8 && static_cast<int64_t>(w / div) * (h / div) > 1500000) div *= 2;
  const int sw = std::max(1, w / div), sh = std::max(1, h / div);
  Canvas src;
  src.setPsram(true);  // a decoded cover is hundreds of KB: the internal heap of the device is about 200 KB (ignored by the simulator)
  src.setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!src.createSprite(sw, sh)) return nullptr;
  src.fillSprite(static_cast<uint32_t>(0xFFFFFFu));
  const uint32_t len = static_cast<uint32_t>(data.size());
  const bool ok = png ? src.drawPng(p, len, 0, 0, sw, sh, 0, 0, 1.0f, 1.0f) : src.drawJpg(p, len, 0, 0, sw, sh, 0, 0, 1.0f / div, 1.0f / div);
  if (!ok) return nullptr;

  auto cv = std::make_shared<Canvas>();
  cv->setPsram(true);
  cv->setColorDepth(lgfx::color_depth_t::grayscale_8bit);
  if (!cv->createSprite(boxW, boxH)) return nullptr;
  cv->fillSprite(static_cast<uint32_t>(0xFFFFFFu));
  const float f = std::min((boxW - 2) / static_cast<float>(sw), (boxH - 2) / static_cast<float>(sh));
  const int ow = std::max(1, static_cast<int>(sw * f)), oh = std::max(1, static_cast<int>(sh * f));
  const int ox = (boxW - ow) / 2, oy = (boxH - oh) / 2;
  const uint8_t* sbuf = static_cast<const uint8_t*>(src.getBuffer());
  uint8_t* dbuf = static_cast<uint8_t*>(cv->getBuffer());
  if (!sbuf || !dbuf) return nullptr;
  for (int y = 0; y < oh; ++y) {
    const int y0 = y * sh / oh, y1 = std::max(y0 + 1, (y + 1) * sh / oh);
    for (int x = 0; x < ow; ++x) {
      const int x0 = x * sw / ow, x1 = std::max(x0 + 1, (x + 1) * sw / ow);
      uint32_t sum = 0;
      for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) sum += sbuf[static_cast<size_t>(yy) * sw + xx];
      dbuf[static_cast<size_t>(oy + y) * boxW + ox + x] = static_cast<uint8_t>(sum / static_cast<uint32_t>((y1 - y0) * (x1 - x0)));
    }
  }
  return cv;
}

}  // namespace pos::ui
