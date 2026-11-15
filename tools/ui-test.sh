#!/usr/bin/env bash
# Runs every tests/ui/*.script headless into out/ui/ and compares each screenshot byte-for-byte
# with tests/ui/golden/<name>.png (docs/SIMULATOR.md "Golden screenshots").
#   tools/ui-test.sh           run and compare (exit 1 on any failure)
#   tools/ui-test.sh --accept  run, then copy out/ui/*.png over the goldens
set -u
cd "$(dirname "$0")/.."
SIM=.pio/build/sim/program
OUT=out/ui
GOLD=tests/ui/golden
ACCEPT=0
[ "${1:-}" = "--accept" ] && ACCEPT=1

[ -x "$SIM" ] || { echo "ui-test: $SIM missing: run make sim"; exit 2; }
rm -rf "$OUT" out/ui-data
mkdir -p "$OUT" "$GOLD" out/ui-data   # empty data root: tests never touch sim-data/

fail=0
shopt -s nullglob
scripts=(tests/ui/*.script)
[ ${#scripts[@]} -gt 0 ] || { echo "ui-test: no scripts in tests/ui"; exit 2; }
for s in "${scripts[@]}"; do
  if ! "$SIM" --headless --script "$s" --out "$OUT" --data out/ui-data --time 2026-01-15T12:34:00 >"$OUT/$(basename "$s").log" 2>&1; then
    echo "FAIL script $s:"
    tail -n 8 "$OUT/$(basename "$s").log" | sed 's/^/    /'
    fail=1
  fi
done

if [ "$ACCEPT" = 1 ]; then
  for png in "$OUT"/*.png; do
    cp "$png" "$GOLD/$(basename "$png")"
    echo "accepted $(basename "$png")"
  done
  exit $fail
fi

ok=0
for png in "$OUT"/*.png; do
  name=$(basename "$png")
  if [ ! -f "$GOLD/$name" ]; then
    echo "NEW   $name has no golden (look at $png, then make ui-accept)"
    fail=1
  elif cmp -s "$png" "$GOLD/$name"; then
    ok=$((ok + 1))
  else
    echo "DIFF  $name differs from $GOLD/$name (new image: $png)"
    fail=1
  fi
done
for g in "$GOLD"/*.png; do
  [ -f "$OUT/$(basename "$g")" ] || { echo "GONE  $(basename "$g"): golden exists, no script produced it"; fail=1; }
done
echo "ui-test: $ok screenshot(s) match"
exit $fail
