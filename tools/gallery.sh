#!/usr/bin/env bash
# Writes out/gallery.html: every UI test screenshot (out/ui/*.png) with its name and golden status.
set -u
cd "$(dirname "$0")/.."
OUT=out/gallery.html
mkdir -p out
{
  echo '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">'
  echo '<title>PaperOS UI gallery</title>'
  echo '<style>body{font:14px system-ui;margin:16px;background:#eee}h1{font-size:18px}'
  echo '.g{display:flex;flex-wrap:wrap;gap:16px}figure{margin:0;background:#fff;padding:8px;border:1px solid #bbb}'
  echo 'img{width:270px;display:block;border:1px solid #999}figcaption{margin-top:6px}.bad{color:#b00}</style>'
  echo "<h1>PaperOS UI gallery</h1><p>$(date '+%Y-%m-%d %H:%M') · from out/ui/ (run make ui-test first)</p><div class=g>"
  shopt -s nullglob
  for png in out/ui/*.png; do
    name=$(basename "$png" .png)
    if [ ! -f "tests/ui/golden/$name.png" ]; then st='<span class=bad>no golden</span>'
    elif cmp -s "$png" "tests/ui/golden/$name.png"; then st='matches golden'
    else st='<span class=bad>differs from golden</span>'; fi
    echo "<figure><img src=\"ui/$name.png\" alt=\"$name\"><figcaption><b>$name</b><br>$st</figcaption></figure>"
  done
  echo '</div>'
} > "$OUT"
echo "wrote $OUT"
