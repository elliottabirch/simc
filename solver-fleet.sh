#!/bin/bash
# Parallel solver_control fleet runner.
#
# solver_control= is a strictly synchronous one-request/one-reply protocol over a
# single FIFO pair, so the engine clamps it to threads=1 and refuses profilesets
# (see sim_t::setup()). Parallelism is therefore PROCESS-level: N independent
# simc processes, each with its own FIFO pair and its own driver instance.
#
# Usage:
#   ./solver-fleet.sh --driver ./my_driver.py --profile ret_base.simc [options]
#
# Options:
#   --driver <cmd>      Driver executable. Invoked as: <cmd> <prefix> <worker-index>
#   --profile <file>    simc profile to run (required)
#   --workers <N>       Parallel processes (default: nproc)
#   --iterations <N>    TOTAL iterations across the fleet (default: 1000); split evenly
#   --seed <N>          Base seed (default: 31459, simc's own deterministic constant)
#   --dump-dir <dir>    Also write decision_dump JSONL per worker into <dir>
#   --out-dir <dir>     Where per-worker stdout/stderr land (default: ./solver-runs/<ts>)
#   --keep-fifos        Don't delete the FIFO directory on exit (debugging)
#
# DRIVER CONTRACT
#   Your driver is invoked once per worker with its own prefix and must:
#     1. open "<prefix>.out" for READ, then "<prefix>.in" for WRITE, in that
#        order. The engine opens .out(write) then .in(read); FIFO opens block
#        until both ends are present, so the mirrored order is what makes the
#        handshake pair up instead of deadlocking.
#     2. read one request line, write exactly one reply line, repeat.
#     3. echo back the request's "seq" verbatim -- the engine aborts the sim on
#        any mismatch.
#     4. exit when it reads {"type":"bye"} (written by solver_control::finish()).
#   Reply types: cast | wait | default | abstain. Malformed JSON, a bad "v", a
#   bad "seq", an unresolvable action name, or an unknown type all abort the sim
#   with a stderr diagnostic -- never a silent wrong answer.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SIMC="$SCRIPT_DIR/build/simc"

DRIVER=""
PROFILE=""
WORKERS="$(nproc 2>/dev/null || echo 4)"
ITERATIONS=1000
# simc uses 31459 when deterministic=1 and no seed is given; matching it here
# means the default fleet is a strict superset of a single deterministic run.
BASE_SEED=31459
DUMP_DIR=""
OUT_DIR=""
KEEP_FIFOS=0

while [ $# -gt 0 ]; do
  case "$1" in
    --driver)     DRIVER="$2"; shift 2 ;;
    --profile)    PROFILE="$2"; shift 2 ;;
    --workers)    WORKERS="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --seed)       BASE_SEED="$2"; shift 2 ;;
    --dump-dir)   DUMP_DIR="$2"; shift 2 ;;
    --out-dir)    OUT_DIR="$2"; shift 2 ;;
    --keep-fifos) KEEP_FIFOS=1; shift ;;
    -h|--help)    sed -n '2,40p' "$0"; exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

[ -x "$SIMC" ]     || { echo "simc not found at $SIMC — run ./build.sh first" >&2; exit 1; }
[ -n "$PROFILE" ]  || { echo "--profile is required" >&2; exit 1; }
[ -f "$PROFILE" ]  || { echo "profile not found: $PROFILE" >&2; exit 1; }
[ -n "$DRIVER" ]   || { echo "--driver is required" >&2; exit 1; }
command -v "$DRIVER" >/dev/null 2>&1 || [ -x "$DRIVER" ] || {
  echo "driver not executable: $DRIVER" >&2; exit 1; }

OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/solver-runs/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT_DIR"
[ -n "$DUMP_DIR" ] && mkdir -p "$DUMP_DIR"

# FIFOs live in their own dir so cleanup is a single rm -rf and cannot glob into
# anything the user cares about.
FIFO_DIR="$(mktemp -d "${TMPDIR:-/tmp}/solver-fleet.XXXXXX")"

pids=()
cleanup() {
  # Kill anything still running before removing the FIFOs -- a driver blocked on
  # a read would otherwise linger holding a deleted pipe.
  for pid in "${pids[@]:-}"; do
    kill "$pid" 2>/dev/null || true
  done
  if [ "$KEEP_FIFOS" -eq 0 ]; then
    rm -rf "$FIFO_DIR"
  else
    echo "FIFOs kept at: $FIFO_DIR" >&2
  fi
}
trap cleanup EXIT INT TERM

# Split total iterations across workers; the remainder is spread one-per-worker
# over the first (ITERATIONS % WORKERS) of them rather than dumped on worker 0,
# so no single process becomes the long pole.
per=$(( ITERATIONS / WORKERS ))
rem=$(( ITERATIONS % WORKERS ))
if [ "$per" -eq 0 ]; then
  echo "--iterations ($ITERATIONS) < --workers ($WORKERS); reducing workers to $ITERATIONS" >&2
  WORKERS="$ITERATIONS"
  per=1
  rem=0
fi

echo "Fleet: $WORKERS workers x ~$per iterations = $ITERATIONS total"
echo "Output: $OUT_DIR"

for i in $(seq 0 $(( WORKERS - 1 )) ); do
  prefix="$FIFO_DIR/w$i"
  mkfifo "$prefix.out" "$prefix.in"

  iters="$per"
  [ "$i" -lt "$rem" ] && iters=$(( per + 1 ))

  # Seed MUST differ per worker. simc seeds as (seed + thread_index), and every
  # process here runs threads=1 so thread_index is 0 in all of them -- with a
  # shared seed the whole fleet would replay byte-identical episodes and N-way
  # parallelism would buy nothing. Offsetting by the worker index reproduces
  # exactly the RNG streams a single threads=N run would have used.
  seed=$(( BASE_SEED + i ))

  simc_args=(
    "$PROFILE"
    "threads=1"
    "iterations=$iters"
    "seed=$seed"
    "solver_control=$prefix"
  )
  [ -n "$DUMP_DIR" ] && simc_args+=( "decision_dump=$DUMP_DIR/w$i.jsonl" )

  # Driver first: it must be ready to open .out for read, since the engine's
  # open of .out for write blocks until a reader appears. Launch order is not
  # strictly required (both sides block), but starting the driver first avoids a
  # spurious stall if the driver is slow to boot.
  "$DRIVER" "$prefix" "$i" > "$OUT_DIR/driver-w$i.log" 2>&1 &
  pids+=( $! )

  "$SIMC" "${simc_args[@]}" > "$OUT_DIR/simc-w$i.log" 2>&1 &
  pids+=( $! )
done

# Collect every exit status rather than bailing on the first failure, so one bad
# worker doesn't hide the results of the other N-1.
failed=0
for pid in "${pids[@]}"; do
  wait "$pid" || failed=$(( failed + 1 ))
done

echo ""
if [ "$failed" -ne 0 ]; then
  echo "FAILED: $failed of ${#pids[@]} processes exited non-zero. Logs: $OUT_DIR" >&2
  grep -lE 'protocol violation|FATAL|Error:' "$OUT_DIR"/*.log 2>/dev/null | sed 's/^/  /' >&2 || true
  exit 1
fi

echo "All $WORKERS workers completed. Logs: $OUT_DIR"
if [ -n "$DUMP_DIR" ]; then
  rows=$(cat "$DUMP_DIR"/w*.jsonl 2>/dev/null | wc -l)
  echo "Decision rows: $rows across $DUMP_DIR/w*.jsonl"
  echo "Note: rows carry (thread, iteration); thread is 0 in every process, so"
  echo "      the fleet-wide combat key is (worker-file, iteration)."
fi
