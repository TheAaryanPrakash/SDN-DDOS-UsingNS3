#!/bin/bash
# Run this INSIDE WSL (as user amalr), not from Windows.
#
# Copies one simulation from this Windows-side repo into the ns-3 "scratch"
# folder, merges in the shared controller/app source files, builds it, and
# (unless -b/--build-only is given) runs it.
#
# Usage:
#   ./sync-build-run.sh <experiment-name> [-- extra ns3 run args]
#   ./sync-build-run.sh <experiment-name> --build-only
#
# Example:
#   ./sync-build-run.sh baseline
#   ./sync-build-run.sh ddos-attack -- --attackRate=5000

set -euo pipefail

WIN_PROJECT="/mnt/c/Users/amalr/OneDrive/Desktop/nwp/sdn-ddos-project"
NS3_DIR="$HOME/sdn-ddos-project/ns-3-dev"

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <experiment-name> [--build-only] [-- extra ns3 run args]" >&2
  exit 1
fi

EXPERIMENT="$1"; shift
BUILD_ONLY=0
if [[ "${1:-}" == "--build-only" || "${1:-}" == "-b" ]]; then
  BUILD_ONLY=1
  shift
fi

SRC_DIR="$WIN_PROJECT/simulations/$EXPERIMENT"
if [[ ! -d "$SRC_DIR" ]]; then
  echo "No such simulation: $SRC_DIR" >&2
  exit 1
fi

DEST_DIR="$NS3_DIR/scratch/$EXPERIMENT"
rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"

cp "$SRC_DIR"/*.cc "$SRC_DIR"/*.h "$DEST_DIR"/ 2>/dev/null || true
cp "$WIN_PROJECT"/src/controller/*.cc "$WIN_PROJECT"/src/controller/*.h "$DEST_DIR"/ 2>/dev/null || true
cp "$WIN_PROJECT"/src/apps/*.cc "$WIN_PROJECT"/src/apps/*.h "$DEST_DIR"/ 2>/dev/null || true

echo "Synced $(ls "$DEST_DIR" | wc -l) file(s) into $DEST_DIR"

cd "$NS3_DIR"
./ns3 build "$EXPERIMENT" -j4

if [[ "$BUILD_ONLY" -eq 1 ]]; then
  echo "Build-only requested, not running."
  exit 0
fi

RESULTS_DIR="$WIN_PROJECT/results/$EXPERIMENT"
mkdir -p "$RESULTS_DIR" "$WIN_PROJECT/logs"
LOG_FILE="$WIN_PROJECT/logs/${EXPERIMENT}-$(date +%Y%m%d-%H%M%S).log"

# --cwd makes ns-3 write its CSV/pcap output (relative paths like
# "switch-stats...csv") directly into this experiment's results folder,
# which is visible from the Windows side.
echo "Running $EXPERIMENT, log: $LOG_FILE, output: $RESULTS_DIR"
./ns3 run "$EXPERIMENT" --cwd="$RESULTS_DIR" "$@" 2>&1 | tee "$LOG_FILE"
