// Emoji for the panel (docs/UI.md "Emoji"): Noto Emoji's monochrome line art as 4-bit alpha bitmaps, one atlas per pixel size on
// the SD card (/sd/paperos/emoji/emoji-<px>.bin, made by tools/gen-emoji.py; `make emoji` copies them to the simulator and the
// device). Only the atlas's index stays in memory (16 bytes per emoji); a bitmap is read when a text first needs it and then
// kept by ui/text's glyph cache, so an emoji draws as fast as a letter afterwards. No card or no atlas: available() is false and
// ui/text falls back to a short text form (":)") or "?".
#pragma once

#include <cstdint>
#include <vector>

namespace pos::ui::emoji {

constexpr const char* kDir = "/sd/paperos/emoji";

struct Bitmap {
  int w = 0, h = 0;
  int x = 0, y = 0;  // top-left corner from the pen on the baseline (y < 0: above it)
  int adv = 0;       // pen advance
  std::vector<uint8_t> levels;  // w * h alpha levels 0..15
};

// The atlas size used for a text of `px` pixels: the largest one not above it (18 at least).
int atlasPx(int px);
// The atlases have this code point (the set is the same in every size). Reads the index on first use (thread-safe).
bool available(uint32_t cp);
// The bitmap of `cp` in the atlas of `px` (an atlasPx() size). False if there is no such atlas or emoji, or the file is broken.
bool load(int px, uint32_t cp, Bitmap& out);
// Forget the loaded indexes and open files (tests switch the data root).
void reset();
// Bitmaps read from the card so far (each emoji and size once; then ui/text's glyph cache has it).
unsigned loads();


}  // namespace pos::ui::emoji
