#!/usr/bin/env bash
# Acceptance run for the real books (tools/fetch-books.sh puts them in sim-data/sd/books/): each book alone on a temporary
# card is opened from the library in the headless simulator and paged to its last page (tests/books/open-and-page.script).
# First and last page go to out/books-check/<name>-first.png / -last.png. Not part of `make check` (needs the downloads, slow).
#   tools/books-check.sh            all books, one after the other
#   tools/books-check.sh PATTERN    only books whose file name contains PATTERN
set -u
cd "$(dirname "$0")/.."
SIM=.pio/build/sim/program
[ -x "$SIM" ] || { echo "books-check: $SIM missing: run make sim"; exit 2; }
OUT=out/books-check
rm -rf "$OUT"; mkdir -p "$OUT"
pattern="${1:-}"
fail=0
shopt -s nullglob
for book in sim-data/sd/books/*.epub sim-data/sd/books/*.txt; do
  name=$(basename "$book")
  case "$name" in *"$pattern"*) ;; *) continue ;; esac
  slug=$(echo "${name%.*}" | tr -c 'A-Za-z0-9' '-' | sed 's/--*/-/g')
  data="$OUT/$slug-data"
  mkdir -p "$data/sd/books"
  cp "$book" "$data/sd/books/"
  start=$(date +%s)
  if "$SIM" --headless --app read --script tests/books/open-and-page.script --out "$OUT/$slug" --data "$data" --time 2026-01-15T12:34:00 >"$OUT/$slug.log" 2>&1; then
    pages=$(grep -o 'reached page [0-9]*/[0-9]*' "$OUT/$slug.log" | head -1)
    opened=$(grep -o "opened '.*'" "$OUT/$slug.log" | head -1)
    printf 'ok   %-62s %s, %s (%ss)\n' "$name" "$opened" "$pages" "$(( $(date +%s) - start ))"
    cp "$OUT/$slug/first-page.png" "$OUT/$slug-first.png"; cp "$OUT/$slug/last-page.png" "$OUT/$slug-last.png"
    case "$name" in "Max Havelaar"*)   # Dutch with accents: look at pages that have them
      mkdir -p "$OUT/$slug-data2/sd/books"; cp "$book" "$OUT/$slug-data2/sd/books/"   # a fresh card: nothing opened yet
      if "$SIM" --headless --app read --script tests/books/accents.script --out "$OUT/$slug" --data "$OUT/$slug-data2" --time 2026-01-15T12:34:00 >"$OUT/$slug-accents.log" 2>&1; then
        for a in "$OUT/$slug"/accent-*.png; do cp "$a" "$OUT/$slug-$(basename "$a")"; done
        echo "ok   $name: pages with accents rendered ($OUT/$slug-accent-*.png)"
      else echo "FAIL $name accents:"; tail -3 "$OUT/$slug-accents.log" | sed 's/^/    /'; fail=1; fi ;;
    esac
  else
    echo "FAIL $name:"; grep -v '^\[disp\]\|^\[touch\]' "$OUT/$slug.log" | tail -5 | sed 's/^/    /'
    fail=1
  fi
done
exit $fail
