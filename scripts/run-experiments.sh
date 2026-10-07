#!/bin/bash
# Run this INSIDE WSL (as user amalr), not from Windows.
#
# Runs the full Phase 11 experiment matrix in one go, each into its own
# results/<name>/ folder (flowmon.xml, monitor-stats.csv, detections.csv,
# switch-stats-*.log) for the Phase 12 graphing scripts to read.
#
# Usage:
#   ./run-experiments.sh            # run everything
#   ./run-experiments.sh exp1 exp3  # run only the named group(s)
#
# Groups: exp1 (normal traffic), exp2 (attack, no mitigation),
# exp3 (attack, mitigated), exp4 (attack-intensity sweep, mitigated),
# exp5 (detection-threshold sweep, mitigated), exp6 (attacker-count sweep,
# mitigated).

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

EXP_GROUPS=("$@")
if [[ ${#EXP_GROUPS[@]} -eq 0 ]]; then
  EXP_GROUPS=(exp1 exp2 exp3 exp4 exp5 exp6)
fi

want() {
  local g
  for g in "${EXP_GROUPS[@]}"; do
    [[ "$g" == "$1" ]] && return 0
  done
  return 1
}

run() {
  echo
  echo "=== $* ==="
  bash sync-build-run.sh "$@"
}

# --- Experiment 1: normal traffic only (the baseline everything else is
# compared against) ---
if want exp1; then
  run normal-traffic --out=exp1-normal
fi

# --- Experiment 2: DDoS attack, no mitigation ---
if want exp2; then
  run ddos-attack --out=exp2-attack-no-mitigation
fi

# --- Experiment 3: DDoS attack, detection + mitigation ---
if want exp3; then
  run ddos-mitigated --out=exp3-attack-mitigated
fi

# --- Experiment 4: attack-intensity sweep (mitigated) ---
# pcap off: these are bulk sweep runs, only the CSV/XML summaries are
# needed for graphing, and pcaps at high attack rates are large and slow.
if want exp4; then
  run ddos-mitigated --out=exp4-intensity-low    -- --attackRate=5Mbps  --enablePcap=false
  run ddos-mitigated --out=exp4-intensity-medium -- --attackRate=20Mbps --enablePcap=false
  run ddos-mitigated --out=exp4-intensity-high   -- --attackRate=50Mbps --enablePcap=false
fi

# --- Experiment 5: detection-threshold sweep (mitigated, fixed attack rate) ---
if want exp5; then
  run ddos-mitigated --out=exp5-threshold-200  -- --threshold=200  --enablePcap=false
  run ddos-mitigated --out=exp5-threshold-1000 -- --threshold=1000 --enablePcap=false
  run ddos-mitigated --out=exp5-threshold-5000 -- --threshold=5000 --enablePcap=false
fi

# --- Experiment 6 (optional): single attacker vs multiple attackers (mitigated) ---
if want exp6; then
  run ddos-mitigated --out=exp6-attackers-1 -- --numAttackers=1 --attackRate=20Mbps --enablePcap=false
  run ddos-mitigated --out=exp6-attackers-3 -- --numAttackers=3 --attackRate=15Mbps --enablePcap=false
fi

echo
echo "=== All requested experiments finished. Results under results/ ==="
