#!/bin/bash
# Run this INSIDE WSL (as user amalr). THE one command for this whole
# project: builds every simulation, runs the full experiment matrix,
# generates all the graphs, and prints a summary of where everything landed.
#
# Usage:
#   cd scripts && bash run-all.sh            # everything
#   cd scripts && bash run-all.sh exp1 exp3  # only these experiment groups
#
# What it does, in order (matches Phase 13 of the project plan):
#   1. build + run       -> scripts/sync-build-run.sh via scripts/run-experiments.sh
#   2. generate results  -> results/<experiment>/{flowmon.xml,monitor-stats.csv,detections.csv,...}
#   3. generate graphs   -> scripts/analysis/generate_graphs.py -> graphs/*.png, results/summary.csv
#   4. inspect           -> this script prints the summary table and graph list at the end

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

echo "################################################################"
echo "# Step 1/3: build + run experiments (several minutes)"
echo "################################################################"
bash run-experiments.sh "$@"

echo
echo "################################################################"
echo "# Step 2/3: generate graphs"
echo "################################################################"
VENV="$HOME/sdn-ddos-project/venv"
if [[ ! -d "$VENV" ]]; then
  echo "No Python venv found -- creating one with matplotlib (one-time setup)..."
  python3 -m venv "$VENV"
  "$VENV/bin/pip" install --quiet --upgrade pip matplotlib
fi
"$VENV/bin/python" analysis/generate_graphs.py

echo
echo "################################################################"
echo "# Step 3/3: results summary"
echo "################################################################"
echo
if command -v column >/dev/null 2>&1; then
  column -s, -t ../results/summary.csv
else
  cat ../results/summary.csv
fi
echo
echo "Graphs:"
ls -1 ../graphs/*.png
echo
echo "Full per-experiment data (flowmon.xml, monitor-stats.csv, detections.csv,"
echo "switch-stats-*.log, and pcaps where enabled) is under results/<experiment-name>/."
echo
echo "Done."
