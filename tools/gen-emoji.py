#!/usr/bin/env python3
"""Emoji atlases for the panel (docs/UI.md "Emoji"): 4-bit alpha bitmaps, one file per pixel size, read from the SD card by
src/ui/emoji.cpp.

Sources, in this order for each code point:
  1. Microsoft Fluent Emoji, "High Contrast" style (MIT, assets/fonts/FluentEmoji-LICENSE.txt): drawn for black and white,
     bold filled shapes; the user chose it over Noto Emoji on 2026-11-16 ("These are ugly. Especially the grandma and
     grandpa"). Not kept in the repo (16 MB of SVG); fetch it first:
       git clone --depth 1 --filter=blob:none --sparse https://github.com/microsoft/fluentui-emoji.git out/fluentui-emoji
       git -C out/fluentui-emoji sparse-checkout set --no-cone '/assets/*/metadata.json' '/assets/*/High Contrast/*.svg' \
         '/assets/*/Default/High Contrast/*.svg' '/LICENSE'
  2. Noto Emoji (Google, SIL OFL 1.1, assets/fonts/NotoEmoji.ttf) for what Fluent does not have.

  DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib uv run -q --no-project --with pillow --with fonttools --with cairosvg \
    python -I tools/gen-emoji.py [out/fluentui-emoji]                writes assets/emoji/emoji-<px>.bin
  (cairosvg needs Homebrew's cairo: brew install cairo)

Format (little endian), version 1:
  header   16 bytes  "PEMJ", u8 version, u8 px, u16 0, u32 count, u32 0
  index    count x 16 bytes, sorted by code point:
           u32 cp, u32 offset of the bitmap from the file start, u8 w, u8 h, i8 x, i8 y, u8 advance, 3 x u8 0
           x, y: the bitmap's top-left corner from the pen position on the baseline (y < 0: above it)
  bitmaps  w * h levels 0..15, two per byte (high nibble first), each bitmap starts on a byte
Code points: single code points from U+2000 up (a trailing U+FE0F is ignored); sequences (ZWJ, keycaps, flags) are not in
the atlas (ui/text shows a ZWJ sequence's first emoji); without the modifiers that never stand alone and without what the
text fonts already draw (U+2010-2027, U+20AC).
"""
import glob
import io
import json
import os
import struct
import sys

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT = os.path.join(ROOT, "assets/fonts/NotoEmoji.ttf")
SIZES = (18, 22, 24, 28)  # caption, body, read-S, title / read-M (src/ui/emoji.cpp maps the other sizes onto these)
WEIGHT = 600              # Noto's wght axis: 400 has hairlines that vanish on the panel, 600 fills small faces
ASCENT = 0.86             # a Fluent emoji's box (its 32 x 32 view box at px x px) sits this much of px above the baseline
OVERSAMPLE = 4            # SVGs are rendered this many times larger, then reduced (sharper than rendering at px)


def skip(c):
    return (0xFE00 <= c <= 0xFE0F or 0x1F3FB <= c <= 0x1F3FF or c in (0x200D, 0x20E3) or 0xE0000 <= c <= 0xE007F
            or 0x2010 <= c <= 0x2027 or c == 0x20AC or c < 0x2000)


def fluent_svgs(repo):
    """{code point: path of its High Contrast SVG} for the single-code-point emoji of the Fluent repo."""
    out = {}
    for meta in glob.glob(os.path.join(repo, "assets", "*", "metadata.json")):
        folder = os.path.dirname(meta)
        seq = [int(x, 16) for x in json.load(open(meta))["unicode"].split()]
        if seq and seq[-1] == 0xFE0F:
            seq = seq[:-1]
        if len(seq) != 1 or skip(seq[0]):
            continue
        svgs = glob.glob(os.path.join(folder, "High Contrast", "*.svg")) or glob.glob(os.path.join(folder, "Default", "High Contrast", "*.svg"))
        if svgs:
            out[seq[0]] = svgs[0]
    return out


def noto_codepoints():
    return sorted(c for c in TTFont(FONT).getBestCmap() if not skip(c))


def render_noto(font, cp, px):
    im = Image.new("L", (px * 4, px * 4), 0)
    pen, base = px, px * 3
    ImageDraw.Draw(im).text((pen, base), chr(cp), font=font, fill=255, anchor="ls")
    adv = int(round(font.getlength(chr(cp))))
    return crop(im, pen, base, adv)


def render_svg(path, px):
    import cairosvg

    big = px * OVERSAMPLE
    png = cairosvg.svg2png(url=path, output_width=big, output_height=big)
    rgba = Image.open(io.BytesIO(png)).convert("RGBA")
    white = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    white.alpha_composite(rgba)
    ink = white.convert("L").point(lambda v: 255 - v).resize((px, px), Image.LANCZOS)  # ink amount, the box px x px
    side = max(2, px // 8)  # room to the letters on both sides, like a glyph's side bearings
    im = Image.new("L", (px * 4, px * 4), 0)
    pen, base = px, px * 3
    im.paste(ink, (pen + side, base - int(round(ASCENT * px))))
    return crop(im, pen, base, px + 2 * side)


def crop(im, pen, base, adv):
    bb = im.getbbox()
    if not bb:
        return 0, 0, 0, 0, adv, b""
    q = im.crop(bb).point(lambda v: (v * 15 + 127) // 255)
    return q.width, q.height, bb[0] - pen, bb[1] - base, adv, q.tobytes()


def build(px, cps, svgs):
    font = ImageFont.truetype(FONT, px)
    font.set_variation_by_axes([WEIGHT])
    glyphs = [render_svg(svgs[cp], px) if cp in svgs else render_noto(font, cp, px) for cp in cps]
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
    repo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "out", "fluentui-emoji")
    svgs = fluent_svgs(repo)
    if not svgs:
        sys.exit(f"no Fluent Emoji in {repo}: fetch it first (see the top of this file)")
    cps = sorted(set(svgs) | set(noto_codepoints()))
    print(f"{len(cps)} emoji: {len(svgs)} from Fluent High Contrast, {len(cps) - len(svgs)} from Noto Emoji")
    out = os.path.join(ROOT, "assets/emoji")
    os.makedirs(out, exist_ok=True)
    for px in SIZES:
        blob = build(px, cps, svgs)
        path = os.path.join(out, f"emoji-{px}.bin")
        with open(path, "wb") as f:
            f.write(blob)
        print(f"{os.path.relpath(path, ROOT)}: {len(blob)} bytes")


main()
