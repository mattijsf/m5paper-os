#!/usr/bin/env python3
"""Emoji atlases for the panel (docs/UI.md "Emoji"): every emoji of Noto Emoji (Google, SIL OFL 1.1; monochrome line art,
which suits 16 grays) as 4-bit alpha bitmaps, one file per pixel size, read from the SD card by src/ui/emoji.cpp.

  uv run -q --no-project --with pillow --with fonttools python -I tools/gen-emoji.py      writes assets/emoji/emoji-<px>.bin

Format (little endian), version 1:
  header   16 bytes  "PEMJ", u8 version, u8 px, u16 0, u32 count, u32 0
  index    count x 16 bytes, sorted by code point:
           u32 cp, u32 offset of the bitmap from the file start, u8 w, u8 h, i8 x, i8 y, u8 advance, 3 x u8 0
           x, y: the bitmap's top-left corner from the pen position on the baseline (y < 0: above it)
  bitmaps  w * h levels 0..15, two per byte (high nibble first), each bitmap starts on a byte
Code points: the font's cmap from U+2000 up, without the modifiers that never stand alone (variation selectors, skin
tones, ZWJ, the keycap mark, tags) and without what the text fonts already draw (U+2010-2027, U+20AC).
"""
import os
import struct
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets/fonts/NotoEmoji.ttf")
SIZES = (18, 22, 24, 28)  # caption, body, read-S, title / read-M (src/ui/emoji.cpp maps the other sizes onto these)
WEIGHT = 600              # the variable font's wght axis: 400 has hairlines that vanish on the panel, 600 fills small faces


def codepoints():
    skip = lambda c: (0xFE00 <= c <= 0xFE0F or 0x1F3FB <= c <= 0x1F3FF or c in (0x200D, 0x20E3) or 0xE0000 <= c <= 0xE007F
                      or 0x2010 <= c <= 0x2027 or c == 0x20AC)
    return sorted(c for c in TTFont(FONT).getBestCmap() if c >= 0x2000 and not skip(c))


def render(font, cp, px):
    im = Image.new("L", (px * 4, px * 4), 0)
    pen, base = px, px * 3
    ImageDraw.Draw(im).text((pen, base), chr(cp), font=font, fill=255, anchor="ls")
    adv = int(round(font.getlength(chr(cp))))
    bb = im.getbbox()
    if not bb:
        return 0, 0, 0, 0, adv, b""
    q = im.crop(bb).point(lambda v: (v * 15 + 127) // 255)
    return q.width, q.height, bb[0] - pen, bb[1] - base, adv, q.tobytes()


def build(px, cps):
    font = ImageFont.truetype(FONT, px)
    font.set_variation_by_axes([WEIGHT])
    glyphs = [render(font, cp, px) for cp in cps]
    data_start = 16 + 16 * len(cps)
    index, data = bytearray(), bytearray()
    for cp, (w, h, x, y, adv, levels) in zip(cps, glyphs):
        if w > 255 or h > 255 or not -128 <= x < 128 or not -128 <= y < 128 or adv > 255:
            sys.exit(f"U+{cp:X} at {px} px does not fit the format")
        index += struct.pack("<IIBBbbB3x", cp, data_start + len(data), w, h, x, y, adv)
        packed = bytearray((len(levels) + 1) // 2)
        for i, v in enumerate(levels):
            packed[i // 2] |= v << 4 if i % 2 == 0 else v
        data += packed
    return struct.pack("<4sBBHII", b"PEMJ", 1, px, 0, len(cps), 0) + bytes(index) + bytes(data)


def main():
    cps = codepoints()
    out = os.path.join(ROOT, "assets/emoji")
    os.makedirs(out, exist_ok=True)
    for px in SIZES:
        blob = build(px, cps)
        path = os.path.join(out, f"emoji-{px}.bin")
        with open(path, "wb") as f:
            f.write(blob)
        print(f"{os.path.relpath(path, ROOT)}: {len(cps)} emoji, {len(blob)} bytes")


main()
