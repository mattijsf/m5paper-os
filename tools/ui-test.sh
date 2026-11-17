#!/usr/bin/env bash
# Runs every tests/ui/*.script headless into out/ui/ and compares each screenshot byte-for-byte
# with tests/ui/golden/<name>.png (docs/SIMULATOR.md "Golden screenshots").
#   tools/ui-test.sh           run and compare (exit 1 on any failure)
#   tools/ui-test.sh --accept  run, then copy out/ui/*.png over the goldens
set -u
cd "$(dirname "$0")/.."
. tools/sim-run.sh   # SIM, run_sim (bounded)
OUT=out/ui
GOLD=tests/ui/golden
ACCEPT=0
[ "${1:-}" = "--accept" ] && ACCEPT=1

[ -x "$SIM" ] || { echo "ui-test: $SIM missing: run make sim"; exit 2; }
rm -rf "$OUT" out/ui-data
mkdir -p "$OUT" "$GOLD" out/ui-data   # one empty data root per script: tests never touch sim-data/ or each other

fail=0
shopt -s nullglob
scripts=(tests/ui/*.script)
[ ${#scripts[@]} -gt 0 ] || { echo "ui-test: no scripts in tests/ui"; exit 2; }
for s in "${scripts[@]}"; do
  name=$(basename "$s" .script)
  rm -rf "out/ui-data/$name"; mkdir -p "out/ui-data/$name"
  [ -d "tests/ui/data/$name" ] && cp -R "tests/ui/data/$name/." "out/ui-data/$name/"   # optional preset data (card file etc.)
  # The emoji atlases are on every card (docs/UI.md "Emoji"): linked, not copied (1.7 MB).
  if [ ! -e "out/ui-data/$name/sd/paperos/emoji" ]; then mkdir -p "out/ui-data/$name/sd/paperos" && ln -s "$PWD/assets/emoji" "out/ui-data/$name/sd/paperos/emoji"; fi
  app=$(sed -n 's/^# app: *//p' "$s" | head -1)   # optional "# app: NAME" header line
  time=$(sed -n 's/^# time: *//p' "$s" | head -1)  # optional "# time: 2026-11-16T09:00:00" header line (default below)
  rtc=$(sed -n 's/^# rtc: *//p' "$s" | head -1)    # optional "# rtc: unset": the clock was never set (--no-rtc) until a time sync
  if ! run_sim "$s" "$OUT" "out/ui-data/$name" ${app:+--app "$app"} ${time:+--time "$time"} ${rtc:+--no-rtc} >"$OUT/$name.script.log" 2>&1; then
    echo "FAIL script $s:"
    tail -n 8 "$OUT/$name.script.log" | sed 's/^/    /'
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
