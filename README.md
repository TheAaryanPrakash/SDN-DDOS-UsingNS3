# SDN-Based DDoS Detection and Mitigation Using NS-3

Final-year networking/security project. Simulates an SDN network in NS-3,
launches a DDoS flood against a victim server, detects the attacker using
simple traffic-rate thresholds (no ML/AI), and mitigates it by pushing an
OpenFlow flow rule from the controller to the switch.

Status: environment and base SDN build verified (see `documentation/environment.md`).
Simulation code has not been written yet — this README will be filled in
fully (architecture, build/run instructions, experiment reproduction steps)
as the project is built out.

## Repo layout

- `simulations/` — our NS-3 `.cc` simulation scripts (the actual network topologies: baseline, attack, mitigation).
- `src/controller/` — custom SDN controller logic (traffic monitoring, threshold detection, mitigation rule installation).
- `src/apps/` — custom NS-3 applications (e.g. the flooding attacker traffic generator), if the built-in ones aren't enough.
- `scripts/` — shell scripts to sync this folder into the WSL NS-3 build and run it, plus Python scripts for turning result CSVs into graphs.
- `results/` — raw CSV/log output from simulation runs, one subfolder per experiment.
- `graphs/` — PNG plots generated from `results/` by the Python scripts.
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
