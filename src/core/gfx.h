// Shared graphics types. Platform independent: compiles for the simulator and the device.
#pragma once

#include <M5GFX.h>
#include <cstdint>

namespace pos {

// Logical screen, portrait. The panel is 960x540 natively; both platforms rotate to portrait.
constexpr int kScreenW = 540;
constexpr int kScreenH = 960;

// The frame canvas: 8-bit grayscale so anti-aliased text blends correctly.
// The panel shows 16 levels; quantisation happens on output (driver / simulator).
using Canvas = lgfx::LGFX_Sprite;

// Colours. ALWAYS pass colours to LovyanGFX as uint32_t 0xRRGGBB: the library picks the colour
// format from the C++ type (uint8_t = RGB332, uint16_t = RGB565, uint32_t/int = RGB888), so a
// uint8_t gray value would be misread as RGB332.
// The panel has 16 gray levels; use only gray(0..15) in UI code.
using Color = uint32_t;
constexpr Color kBlack = 0x000000u;
constexpr Color kWhite = 0xFFFFFFu;
constexpr Color gray(int level16) { return static_cast<Color>(level16 * 17) * 0x010101u; }  // 0 black .. 15 white

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;
  bool empty() const { return w <= 0 || h <= 0; }
};

// How a region is driven on the panel. Maps to lgfx::epd_mode_t on the device.
enum class Refresh : uint8_t {
  Quality,  // full 16-gray waveform, flashes. Images, cleaning ghosts.
  Text,     // 16-gray, no flash. Page turns, lists.
  Fast,     // black/white only, quick. Touch feedback, toggles.
};

}  // namespace pos
