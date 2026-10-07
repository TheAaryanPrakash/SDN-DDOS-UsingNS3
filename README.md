# SDN-Based DDoS Detection and Mitigation Using NS-3

Final-year networking/security project. Simulates an SDN network in NS-3,
launches a DDoS flood against a victim server, detects the attacker using
simple traffic-rate thresholds (no ML/AI), and mitigates it by pushing an
OpenFlow flow rule from the controller to the switch.

Status: through Phase 12 of 13 (environment, SDN build, controller with
monitoring/detection/mitigation, the full experiment matrix, and graphs are
done). This README will get a full pass in Phase 13 (final project); for
now, the essentials below are enough to build on.

## Quick start (inside WSL, as user `amalr`)

```bash
cd scripts
bash sync-build-run.sh ddos-mitigated          # build + run one simulation
bash run-experiments.sh                        # run the full experiment matrix
```

`sync-build-run.sh <experiment> [--out=<name>] [-- <sim args>]` copies one
simulation from `simulations/<experiment>/` plus the shared controller code
from `src/` into the WSL-side ns-3 build, builds just that target, and runs
it with output landing in `results/<name>/` (defaults to `results/<experiment>/`).

`run-experiments.sh [group ...]` runs the whole Phase 11 experiment matrix
(or just the named group(s): `exp1`..`exp6`) -- see "Experiments" below.

## Simulations

| Folder | Purpose |
|---|---|
| `simulations/baseline/` | Phase 5: smallest working SDN network (1 client, 1 server), connectivity sanity check. |
| `simulations/normal-traffic/` | Phase 6: multiple legitimate clients, no attack. Also Experiment 1. |
| `simulations/ddos-attack/` | Phase 7: adds one attacker; mitigation explicitly disabled. Experiment 2. |
| `simulations/ddos-mitigated/` | Phase 10: same topology, detection + mitigation active; supports `--numAttackers`, `--attackRate`, `--threshold`, `--enablePcap`. Drives Experiments 3-6. |

All of them share one controller: `src/controller/sdn-controller.{h,cc}`
(`SdnController`) -- L2 learning switch + OpenFlow port-stats monitoring +
threshold detection + DROP-flow mitigation, all in one class, with
mitigation toggleable per run via the `EnableMitigation` attribute.

## Experiments (Phase 11)

Run via `bash run-experiments.sh`, output in `results/exp<N>-...`:

| # | Name | What it varies |
|---|---|---|
| 1 | `exp1-normal` | Normal traffic only (baseline). |
| 2 | `exp2-attack-no-mitigation` | Attack, detection logs but no blocking. |
| 3 | `exp3-attack-mitigated` | Attack, detection + mitigation. |
| 4 | `exp4-intensity-{low,medium,high}` | Attack rate: 5 / 20 / 50 Mbps, mitigated. |
| 5 | `exp5-threshold-{200,1000,5000}` | Detection threshold (pkt/s), fixed 20Mbps attack, mitigated. |
| 6 | `exp6-attackers-{1,3}` | Single attacker vs. 3 simultaneous attackers, mitigated. |

Each run produces, in its `results/` subfolder:
- `flowmon.xml` -- real per-flow throughput/delay/loss (ns-3 FlowMonitor).
- `monitor-stats.csv` -- per-port packets/sec and bytes/sec over time (our controller's own OpenFlow port-stats polling).
- `detections.csv` -- every DETECTED/CLEARED (and _AND_BLOCKED/_AND_UNBLOCKED) transition, with simulation timestamps.
- `switch-stats-*.log` -- OFSwitch13's own datapath performance counters.
- `host-*.pcap` -- per-port packet captures (skipped when a run passes `--enablePcap=false`, used for the bulk sweep experiments 4-6 to save time/disk).

## Graphs (Phase 12)

After `run-experiments.sh` has populated `results/`, generate the graphs
(runs on plain Windows/WSL Python + matplotlib, no ns-3 needed -- it only
reads the CSV/XML files the simulations already wrote):

```bash
cd scripts/analysis
python generate_graphs.py
```

This reads `results/exp*/flowmon.xml` and `detections.csv`, writes four
PNGs to `graphs/`, and a per-experiment `results/summary.csv`. Every number
plotted is read from a real simulation output file -- nothing here is
estimated or hand-entered. `scripts/analysis/common.py` has the parsing
logic if you want to reuse it for extra plots.

| File | Shows |
|---|---|
| `01_before_after_mitigation.png` | Legit loss % and throughput: normal vs. unmitigated attack vs. mitigated attack. The project's headline result. |
| `02_attack_intensity_sweep.png` | Legit throughput/loss/delay vs. attack rate (5/20/50 Mbps), mitigation active. |
| `03_detection_threshold_sweep.png` | Detection latency and legit loss vs. threshold (200/1000/5000 pkt/s). Bars are flat because this particular attack is far above all three thresholds -- a real result, not a bug (see `documentation/environment.md`). |
| `04_multi_attacker_comparison.png` | Legit loss and detection latency: 1 vs. 3 simultaneous attackers. |

## Repo layout

- `simulations/` — our NS-3 `.cc` simulation scripts (the actual network topologies: baseline, attack, mitigation).
- `src/controller/` — custom SDN controller logic (traffic monitoring, threshold detection, mitigation rule installation).
- `src/apps/` — custom NS-3 applications (e.g. the flooding attacker traffic generator), if the built-in ones aren't enough.
- `scripts/` — `sync-build-run.sh`/`run-experiments.sh` (sync into WSL NS-3 build and run), `scripts/analysis/` (Python: turn `results/` into graphs).
- `results/` — raw CSV/XML/log output from simulation runs, one subfolder per experiment, plus `summary.csv` (Phase 12).
- `graphs/` — PNG plots generated from `results/` by `scripts/analysis/generate_graphs.py`.
- `logs/` — build and run logs kept for debugging/reproducibility.
- `documentation/` — environment notes, design decisions, diagrams.

## Where NS-3 itself lives

NS-3 and OFSwitch13 are large, build-heavy codebases and live natively inside
WSL (not under this Windows folder) for build performance:

```
~/sdn-ddos-project/ns-3-dev/            (WSL, native ext4 filesystem)
~/sdn-ddos-project/ns-3-dev/contrib/ofswitch13/
```

Files under this repo's `simulations/` and `src/` get copied into
`ns-3-dev/scratch/` (or `ns-3-dev/contrib/`) inside WSL before building/running.
See `scripts/` once it's populated.
