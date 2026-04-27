#!/bin/bash
# Dynamic APL variant comparison runner.
#
# Discovers every apls/*.simc file and runs it as a variant against the stock
# character defined in ret_base.simc, across every scenario in scenarios/
# (or a single named scenario if passed as an argument).
#
# Usage:
#   ./sim.sh                  # all scenarios, 50k iter
#   ./sim.sh 1t_5min          # single scenario
#   ITERATIONS=5000 ./sim.sh  # override iteration count (useful for smoke tests)

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SIMC="$SCRIPT_DIR/build/simc"
BASE="$SCRIPT_DIR/ret_base.simc"
APLS_DIR="$SCRIPT_DIR/apls"
SCENARIOS_DIR="$SCRIPT_DIR/scenarios"
REPORTS_DIR="$SCRIPT_DIR/reports"
ITERATIONS="${ITERATIONS:-10000}"

if [ ! -x "$SIMC" ]; then
  echo "simc binary not found at $SIMC — run ./build.sh release first" >&2
  exit 1
fi
if [ ! -f "$BASE" ]; then
  echo "baseline profile not found at $BASE" >&2
  exit 1
fi

mkdir -p "$REPORTS_DIR"

# Discover variants (only .simc files, skip README.md etc.)
shopt -s nullglob
variants=("$APLS_DIR"/*.simc)
if [ ${#variants[@]} -eq 0 ]; then
  echo "No variants found in $APLS_DIR — running stock baseline only."
fi

# Select scenarios
if [ $# -ge 1 ]; then
  scenario_file="$SCENARIOS_DIR/$1.simc"
  if [ ! -f "$scenario_file" ]; then
    echo "Scenario '$1' not found at $scenario_file" >&2
    echo "Available:" >&2
    for f in "$SCENARIOS_DIR"/*.simc; do echo "  $(basename "$f" .simc)" >&2; done
    exit 1
  fi
  scenarios=("$scenario_file")
else
  scenarios=("$SCENARIOS_DIR"/*.simc)
fi

for scenario_file in "${scenarios[@]}"; do
  scenario="$(basename "$scenario_file" .simc)"
  report="$REPORTS_DIR/$scenario.html"

  echo ""
  echo "=== Scenario: $scenario (${#variants[@]} variants + stock, $ITERATIONS iter) ==="

  # Order matters: base first (defines stock actor with gear + stock APL),
  # then scenario (sets sim-wide target count + fight length, and applies the
  # scenario-specific talent string to the stock actor). Subsequent copy=
  # clones pick up that talent automatically.
  args=("$BASE" "$scenario_file")
  for apl in "${variants[@]}"; do
    name="$(basename "$apl" .simc)"
    args+=("copy=$name,stock" "$apl")
  done
  args+=("iterations=$ITERATIONS" "html=$report")

  "$SIMC" "${args[@]}"

  echo "Report: $report"
done
