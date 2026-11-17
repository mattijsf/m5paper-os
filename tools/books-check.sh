#!/usr/bin/env bash
# Acceptance run for the real books (tools/fetch-books.sh puts them in sim-data/sd/books/): each book alone on a temporary
# card is opened from the library in the headless simulator and paged to its last page (tests/books/open-and-page.script).
# First and last page go to out/books-check/<name>-first.png / -last.png. Not part of `make check` (needs the downloads, slow).
#   tools/books-check.sh            all books, one after the other
#   tools/books-check.sh PATTERN    only books whose file name contains PATTERN
set -u
cd "$(dirname "$0")/.."
SIM_LIMIT=${SIM_LIMIT:-900}   # a long novel paged to its end takes minutes
. tools/sim-run.sh            # SIM, run_sim (bounded)
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
  if run_sim tests/books/open-and-page.script "$OUT/$slug" "$data" --app read >"$OUT/$slug.log" 2>&1; then
    pages=$(grep -o 'reached page [0-9]*/[0-9]*' "$OUT/$slug.log" | head -1)
    opened=$(grep -o "opened '.*'" "$OUT/$slug.log" | head -1)
    printf 'ok   %-62s %s, %s (%ss)\n' "$name" "$opened" "$pages" "$(( $(date +%s) - start ))"
    cp "$OUT/$slug/first-page.png" "$OUT/$slug-first.png"; cp "$OUT/$slug/last-page.png" "$OUT/$slug-last.png"
    case "$name" in "Pride and Prejudice"*)   # the same page in every text size, and the menu
      mkdir -p "$OUT/$slug-data3/sd/books"; cp "$book" "$OUT/$slug-data3/sd/books/"
      if run_sim tests/books/sizes.script "$OUT/$slug" "$OUT/$slug-data3" --app read >"$OUT/$slug-sizes.log" 2>&1; then
        for a in menu size-s size-m size-l size-xl; do cp "$OUT/$slug/$a.png" "$OUT/$slug-$a.png"; done
        echo "ok   $name: text sizes ($OUT/$slug-size-*.png)"
      else echo "FAIL $name sizes:"; tail -3 "$OUT/$slug-sizes.log" | sed 's/^/    /'; fail=1; fi ;;
    esac
    case "$name" in "Max Havelaar"*)   # Dutch with accents: look at pages that have them
      mkdir -p "$OUT/$slug-data2/sd/books"; cp "$book" "$OUT/$slug-data2/sd/books/"   # a fresh card: nothing opened yet
      if run_sim tests/books/accents.script "$OUT/$slug" "$OUT/$slug-data2" --app read >"$OUT/$slug-accents.log" 2>&1; then
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
