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
  2. Noto Emoji (Google, SIL OFL 1.1, assets/fonts/NotoEmoji.ttf) for single code points Fluent does not have.
  3. Flags are drawn here as their letters in an outlined box ("NL", like the keycaps): on 16 grays a flag is mostly
     stripes of gray. Every pair of regional
     indicators A-Z (676; a code nobody uses still shows what was sent) and England, Scotland, Wales (ENG, SCO, WAL), in
     Inter SemiBold (assets/fonts).

  DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib uv run -q --no-project --with pillow --with fonttools --with cairosvg \
    python -I tools/gen-emoji.py [out/fluentui-emoji]                writes assets/emoji/emoji-<px>.bin
  (cairosvg needs Homebrew's cairo: brew install cairo)

Format (little endian), version 2:
  header   16 bytes  "PEMJ", u8 version, u8 px, u16 0, u32 count, u32 offset of the sequence table (0: none)
  index    count x 16 bytes, sorted by code point:
           u32 cp, u32 offset of the bitmap from the file start, u8 w, u8 h, i8 x, i8 y, u8 advance, 3 x u8 0
           x, y: the bitmap's top-left corner from the pen position on the baseline (y < 0: above it)
  bitmaps  w * h levels 0..15, two per byte (high nibble first), each bitmap starts on a byte
  sequences u32 n, then n times: u32 id, u8 length, 3 x u8 0, length x u32 code points; sorted by code points
Code points: single code points from U+2000 up (a trailing U+FE0F is ignored), without the modifiers that never stand alone
and without what the text fonts already draw (U+2010-2027, U+20AC). Sequences (ZWJ sequences, keycaps, flags; U+FE0F and
skin tones left out) get an id from U+F0000 up (Supplementary Private Use Area-A): ui/text replaces a sequence it finds in
a text by its id, and the id is an ordinary entry of the index.
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
SEQ_BASE = 0xF0000        # the ids of sequences (src/ui/emoji.cpp)
FLAG_FONT = os.path.join(ROOT, "assets/fonts/Inter-SemiBold.ttf")
TAG_FLAGS = {"gbeng": "ENG", "gbsct": "SCO", "gbwls": "WAL"}  # subdivision flags: black flag + tag letters + cancel tag


def skip(c):
    return (0xFE00 <= c <= 0xFE0F or 0x1F3FB <= c <= 0x1F3FF or c in (0x200D, 0x20E3) or 0xE0000 <= c <= 0xE007F
            or 0x2010 <= c <= 0x2027 or c == 0x20AC or c < 0x2000 or c >= SEQ_BASE)  # Noto's own private-use glyphs too


def fluent_svgs(repo, sequences=False):
    """{code point: path of its High Contrast SVG} for the single-code-point emoji of the Fluent repo; with `sequences`,
    {tuple of code points (without U+FE0F): path} for its sequences instead."""
    out = {}
    for meta in glob.glob(os.path.join(repo, "assets", "*", "metadata.json")):
        folder = os.path.dirname(meta)
        seq = [int(x, 16) for x in json.load(open(meta))["unicode"].split() if int(x, 16) != 0xFE0F]
        svgs = glob.glob(os.path.join(folder, "High Contrast", "*.svg")) or glob.glob(os.path.join(folder, "Default", "High Contrast", "*.svg"))
        if not svgs or not seq or any(0x1F3FB <= c <= 0x1F3FF for c in seq):
            continue
        if sequences and len(seq) > 1:
            out[tuple(seq)] = svgs[0]
        elif not sequences and len(seq) == 1 and not skip(seq[0]):
            out[seq[0]] = svgs[0]
    return out


def flags():
    """[(code points, letters)]: every regional indicator pair, and the three tag flags."""
    out = []
    for a in range(26):
        for b in range(26):
            out.append(((0x1F1E6 + a, 0x1F1E6 + b), chr(65 + a) + chr(65 + b)))
    for tag, letters in TAG_FLAGS.items():
        out.append(((0x1F3F4,) + tuple(0xE0000 + ord(c) for c in tag) + (0xE007F,), letters))
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


def render_flag(letters, px):
    """The letters in an outlined, rounded box: the box as high as the emoji's middle part, centred on it."""
    o = OVERSAMPLE
    box_h = round(px * 0.78) * o
    line = max(1, round(px / 18)) * o  # 1 px up to 24, 2 at 28
    font = ImageFont.truetype(FLAG_FONT, round(px * 0.64) * o)
    tw = font.getlength(letters)
    pad = round(px * 0.13) * o
    box_w = int(tw) + 2 * pad
    side = max(2, px // 8)
    big = Image.new("L", (box_w, box_h), 0)
    d = ImageDraw.Draw(big)
    d.rounded_rectangle((line // 2, line // 2, box_w - 1 - line // 2, box_h - 1 - line // 2), radius=round(px * 0.14) * o,
                        outline=255, width=line)
    d.text((box_w / 2, box_h / 2), letters, font=font, fill=255, anchor="mm")
    small = big.resize((max(1, round(box_w / o)), round(box_h / o)), Image.LANCZOS)
    im = Image.new("L", (px * 6, px * 4), 0)
    pen, base = px, px * 3
    mid = base - round(px * 0.36)  # the middle of an emoji's box (ASCENT above, the rest below the baseline)
    im.paste(small, (pen + side, mid - small.height // 2))
    return crop(im, pen, base, small.width + 2 * side)


def crop(im, pen, base, adv):
    bb = im.getbbox()
    if not bb:
        return 0, 0, 0, 0, adv, b""
    q = im.crop(bb).point(lambda v: (v * 15 + 127) // 255)
    return q.width, q.height, bb[0] - pen, bb[1] - base, adv, q.tobytes()


def build(px, cps, svgs, seqs):
    """`seqs`: [(id, code points, SVG path or None, flag letters or None)], ids from SEQ_BASE up in this order."""
    font = ImageFont.truetype(FONT, px)
    font.set_variation_by_axes([WEIGHT])
    glyphs = [render_svg(svgs[cp], px) if cp in svgs else render_noto(font, cp, px) for cp in cps]
    glyphs += [render_svg(path, px) if path else render_flag(letters, px) for _, _, path, letters in seqs]
    cps = list(cps) + [sid for sid, _, _, _ in seqs]
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
    table = bytearray(struct.pack("<I", len(seqs)))
    for sid, seq, _, _ in sorted(seqs, key=lambda s: s[1]):
        table += struct.pack("<IB3x", sid, len(seq)) + b"".join(struct.pack("<I", c) for c in seq)
    while len(data) % 4:
        data.append(0)
    seq_offset = data_start + len(data)
    return struct.pack("<4sBBHII", b"PEMJ", 2, px, 0, len(cps), seq_offset) + bytes(index) + bytes(data) + bytes(table)


def main():
    repo = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "out", "fluentui-emoji")
    svgs = fluent_svgs(repo)
    if not svgs:
        sys.exit(f"no Fluent Emoji in {repo}: fetch it first (see the top of this file)")
    cps = sorted(set(svgs) | set(noto_codepoints()))
    joined = fluent_svgs(repo, sequences=True)
    flag_list = flags()
    seqs = [(seq, path, None) for seq, path in sorted(joined.items())] + [(seq, None, letters) for seq, letters in flag_list]
    seqs = [(SEQ_BASE + i, seq, path, letters) for i, (seq, path, letters) in enumerate(seqs)]
    print(f"{len(cps)} emoji: {len(svgs)} from Fluent High Contrast, {len(cps) - len(svgs)} from Noto Emoji; "
          f"{len(joined)} sequences from Fluent, {len(flag_list)} flags as letters")
    out = os.path.join(ROOT, "assets/emoji")
    os.makedirs(out, exist_ok=True)
    for px in SIZES:
        blob = build(px, cps, svgs, seqs)
        path = os.path.join(out, f"emoji-{px}.bin")
        with open(path, "wb") as f:
            f.write(blob)
        print(f"{os.path.relpath(path, ROOT)}: {len(blob)} bytes")


main()
