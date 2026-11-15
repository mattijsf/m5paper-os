#!/usr/bin/env bash
# Bakes fonts and icons into src/gen/ (docs/UI.md "Type", "Icons"; docs/DECISIONS.md D12).
#   tools/gen-assets.sh
# Needs Node (npx) and python3. Output (src/gen/fonts.{h,cpp}, icons.h) is committed, so normal
# builds need neither. Fonts: lv_font_conv binary format (`--format bin`), which M5GFX loads with
# loadFont(data, ft_lvgl) (its BFFfont). Source TTFs and licences: assets/fonts/.
set -euo pipefail
cd "$(dirname "$0")/.."

LV_FONT_CONV="lv_font_conv@1.5.3"      # pinned
RANGES="0x20-0x7E,0xA0-0xFF,0x100-0x17F,0x2010-0x2027,0x20AC"   # docs/UI.md "Glyph ranges"
BUILD=out/gen-assets
rm -rf "$BUILD"; mkdir -p "$BUILD" src/gen

conv() {  # conv <id> <ttf> <size> <ranges>
  npx --yes "$LV_FONT_CONV" --font "assets/fonts/$2" --size "$3" --bpp 4 --format bin -r "$4" -o "$BUILD/$1.bin"
}

#    id                      ttf                      px
conv inter_regular_18        Inter-Regular.ttf        18 "$RANGES"
conv inter_regular_22        Inter-Regular.ttf        22 "$RANGES"
conv inter_semibold_22       Inter-SemiBold.ttf       22 "$RANGES"
conv inter_semibold_28       Inter-SemiBold.ttf       28 "$RANGES"
conv inter_semibold_40       Inter-SemiBold.ttf       40 "$RANGES"
conv literata_regular_24     Literata-Regular.ttf     24 "$RANGES"
conv literata_regular_28     Literata-Regular.ttf     28 "$RANGES"
conv literata_regular_32     Literata-Regular.ttf     32 "$RANGES"
conv literata_regular_38     Literata-Regular.ttf     38 "$RANGES"
conv literata_italic_28      Literata-Italic.ttf      28 "$RANGES"
conv literata_bold_28        Literata-Bold.ttf        28 "$RANGES"

# Icons: code points of the names in assets/icons.txt.
ICON_RANGES=$(python3 tools/gen_assets.py icon-ranges)
conv lucide_32 lucide.ttf 32 "$ICON_RANGES"
conv lucide_64 lucide.ttf 64 "$ICON_RANGES"

python3 tools/gen_assets.py emit "$BUILD"
echo "generated src/gen/fonts.h src/gen/fonts.cpp src/gen/icons.h"
