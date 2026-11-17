# Sourced by tools/ui-test.sh and tools/books-check.sh: one way to run the simulator headless for a script.
#   run_sim SCRIPT OUT DATA [more simulator arguments]
# The clock starts at the same moment for every run (screenshots do not depend on the day), and a run that hangs is
# killed after SIM_LIMIT seconds (default 120) instead of blocking make check for ever: its exit code is then 142.
SIM=${SIM:-.pio/build/sim/program}
SIM_TIME=2026-01-15T12:34:00

run_sim() {
  local script=$1 out=$2 data=$3
  shift 3
  local start="--time $SIM_TIME" a   # (no array: bash 3.2 with set -u trips over an empty one)
  for a in "$@"; do
    if [ "$a" = "--time" ]; then start=""; fi   # the caller gives its own start time
  done
  # shellcheck disable=SC2086
  perl -e 'alarm shift; exec @ARGV or die "cannot run $ARGV[0]: $!\n"' "${SIM_LIMIT:-120}" \
    "$SIM" --headless --script "$script" --out "$out" --data "$data" $start "$@"
}
