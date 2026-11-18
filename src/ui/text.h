// Text and icons on the canvas: baked Inter / Literata / Lucide fonts (src/gen, tools/gen-assets.sh).
// docs/UI.md "Type" and "Icons". Strings are UTF-8; code points outside the baked ranges draw as '?'.
#pragma once

#include <string>

#include "core/gfx.h"
#include "gen/icons.h"

namespace pos::ui {

enum class Type : uint8_t {
  Caption,      // Inter Regular 18
  Body,         // Inter Regular 22
  BodyStrong,   // Inter SemiBold 22
  Title,        // Inter SemiBold 28
  Headline,     // Inter SemiBold 40
  ReadS,        // Literata Regular 24
  ReadM,        // Literata Regular 28
  ReadL,        // Literata Regular 32
  ReadXL,       // Literata Regular 38
  ReadItalicM,  // Literata Italic 28
  ReadBoldM,    // Literata Bold 28
  Mono,         // JetBrains Mono Medium 22: every character 13 px wide (Teletekst's 40-column grid)
  Count
};

using Icon = gen::Icon;

// Where (x, y) sits on the text's box.
enum class Anchor : uint8_t { TopLeft, TopCenter, TopRight, MiddleLeft, MiddleCenter, MiddleRight };

// Draws `utf8` in `color`, anti-aliased against `bg`. M5GFX blends glyph edges between the text
// colour and an explicit background colour (it cannot read the canvas), and fills each glyph cell
// with `bg`: pass the colour the text sits on (paper, fill, ink for white-on-black). Text over a
// picture is not supported.
void drawText(Canvas& c, const std::string& utf8, int x, int y, Type type, Color color,
              Anchor anchor = Anchor::TopLeft, Color bg = 0xFFFFFFu);
// Forget which font a canvas has loaded (call when a canvas on this thread is created or destroyed).
void forgetLoadedFont();
// Width in pixels of the text on one line.
int measureText(Canvas& c, const std::string& utf8, Type type);
// How far drawText moves the pen over the text: where a second piece drawn after it has to start. For the Inter faces
// this is a little less than measureText (their glyph advances are fractions of a pixel: LovyanGFX measures them rounded
// and draws them cut off, about half a pixel a character; docs/STATUS.md "Notes for later tasks").
int penAdvance(Canvas& c, const std::string& utf8, Type type);
// Line height in pixels of the font.
int lineHeight(Canvas& c, Type type);
// Cuts the text to fit `maxWidth` and ends it with an ellipsis when it was cut (UTF-8 aware).
std::string ellipsize(Canvas& c, const std::string& utf8, Type type, int maxWidth);
// Replaces invalid UTF-8 and code points that have no glyph by '?'.
std::string sanitize(const std::string& utf8);

// Test aid: false draws every string through LovyanGFX (the reference the fast path must match byte for byte).
void setFastText(bool on);
unsigned fastTextDraws();  // strings drawn by the fast path on this thread (tests)

// Draws a Lucide icon of `size` (32 or 64) with its box centred on (cx, cy).
void drawIcon(Canvas& c, Icon icon, int cx, int cy, int size, Color color, Color bg = 0xFFFFFFu);

}  // namespace pos::ui
