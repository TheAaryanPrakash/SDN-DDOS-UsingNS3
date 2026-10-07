"""
Phase 12: turn the real results from Phase 11's experiment matrix into
graphs and a summary table. Every number plotted here is read from a
results/<experiment>/flowmon.xml or detections.csv file that a simulation
actually produced -- nothing in this script invents data.

Run from anywhere (paths are resolved relative to this file):
    python scripts/analysis/generate_graphs.py

Requires: matplotlib (everything else is Python standard library).
Writes PNGs to graphs/, and a summary table to results/summary.csv.
"""

import csv

import matplotlib.pyplot as plt

from common import (
    GRAPHS_DIR,
    RESULTS_DIR,
    experiment_detections,
    experiment_flowmon,
    first_detection_time,
    legit_summary,
    load_detections,
    load_flows,
)

SIM_DURATION_SEC = 20.0
ATTACK_START_SEC = 10.0

GRAPHS_DIR.mkdir(exist_ok=True)


def summarize(name):
    """One experiment's legit-traffic summary + detection time, or None
    fields where an experiment has no results (e.g. not yet run)."""
    flowmon = experiment_flowmon(name)
    if not flowmon.exists():
        print(f"  (skipping {name}: no flowmon.xml found -- run it first)")
        return None

    flows = load_flows(flowmon)
    summary = legit_summary(flows, SIM_DURATION_SEC)

    detections = load_detections(experiment_detections(name))
    det_time = first_detection_time(detections)
    summary["detectionDelaySec"] = (det_time - ATTACK_START_SEC) if det_time else None
    summary["name"] = name
    return summary


def fig_before_after():
    names = ["exp1-normal", "exp2-attack-no-mitigation", "exp3-attack-mitigated"]
    labels = ["Normal", "Attack\n(no mitigation)", "Attack\n(mitigated)"]
    data = [summarize(n) for n in names]
    if any(d is None for d in data):
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))

    ax1.bar(labels, [d["lossPct"] for d in data], color=["#4C9A2A", "#C0392B", "#2E86AB"])
    ax1.set_ylabel("Legitimate client packet loss (%)")
    ax1.set_title("Packet loss: before/after mitigation")

    ax2.bar(labels, [d["throughputMbps"] for d in data], color=["#4C9A2A", "#C0392B", "#2E86AB"])
    ax2.set_ylabel("Legitimate client throughput (Mbps)")
    ax2.set_title("Throughput: before/after mitigation")

    fig.tight_layout()
    out = GRAPHS_DIR / "01_before_after_mitigation.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out}")


def fig_intensity_sweep():
    names = ["exp4-intensity-low", "exp4-intensity-medium", "exp4-intensity-high"]
    rates_mbps = [5, 20, 50]
    data = [summarize(n) for n in names]
    if any(d is None for d in data):
        return

    fig, axes = plt.subplots(1, 3, figsize=(14, 4.5))

    axes[0].plot(rates_mbps, [d["throughputMbps"] for d in data], marker="o", color="#2E86AB")
    axes[0].set_xlabel("Attack rate (Mbps)")
    axes[0].set_ylabel("Legit throughput (Mbps)")
    axes[0].set_title("Throughput vs. attack intensity")

    axes[1].plot(rates_mbps, [d["lossPct"] for d in data], marker="o", color="#C0392B")
    axes[1].set_xlabel("Attack rate (Mbps)")
    axes[1].set_ylabel("Legit packet loss (%)")
    axes[1].set_title("Packet loss vs. attack intensity")

    axes[2].plot(rates_mbps, [d["avgDelayMs"] for d in data], marker="o", color="#8E44AD")
    axes[2].set_xlabel("Attack rate (Mbps)")
    axes[2].set_ylabel("Legit avg. delay (ms)")
    axes[2].set_title("Delay vs. attack intensity")

    for ax in axes:
        ax.set_xticks(rates_mbps)

    fig.suptitle("Experiment 4: attack intensity sweep (mitigation active)")
    fig.tight_layout()
    out = GRAPHS_DIR / "02_attack_intensity_sweep.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out}")


def fig_threshold_sweep():
    names = ["exp5-threshold-200", "exp5-threshold-1000", "exp5-threshold-5000"]
    thresholds = [200, 1000, 5000]
    data = [summarize(n) for n in names]
    if any(d is None for d in data):
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(10, 4.5))

    ax1.bar([str(t) for t in thresholds], [d["detectionDelaySec"] or 0 for d in data],
            color="#E67E22")
    ax1.set_xlabel("Detection threshold (packets/sec)")
    ax1.set_ylabel("Detection delay (s, after attack starts)")
    ax1.set_title("Detection latency vs. threshold")

    ax2.bar([str(t) for t in thresholds], [d["lossPct"] for d in data], color="#2E86AB")
    ax2.set_xlabel("Detection threshold (packets/sec)")
    ax2.set_ylabel("Legit packet loss (%)")
    ax2.set_title("Legit loss vs. threshold")

    fig.suptitle(
        "Experiment 5: detection threshold sweep (fixed 20Mbps attack)\n"
        "Note: this attack (~39,062 pkt/s) is far above all three thresholds tested,\n"
        "so detection timing is similar across them -- see documentation/environment.md"
    )
    fig.tight_layout()
    out = GRAPHS_DIR / "03_detection_threshold_sweep.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out}")


def fig_multi_attacker():
    names = ["exp6-attackers-1", "exp6-attackers-3"]
    labels = ["1 attacker\n(20Mbps)", "3 attackers\n(15Mbps each)"]
    data = [summarize(n) for n in names]
    if any(d is None for d in data):
        return

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9, 4.5))

    ax1.bar(labels, [d["lossPct"] for d in data], color=["#2E86AB", "#C0392B"])
    ax1.set_ylabel("Legit packet loss (%)")
    ax1.set_title("Loss: single vs. multiple attackers")

    detection_delays = [d["detectionDelaySec"] or 0 for d in data]
    ax2.bar(labels, detection_delays, color=["#2E86AB", "#C0392B"])
    ax2.set_ylabel("Detection delay (s, first attacker detected)")
    ax2.set_title("Detection latency: single vs. multiple attackers")

    fig.suptitle("Experiment 6: single attacker vs. multiple simultaneous attackers")
    fig.tight_layout()
    out = GRAPHS_DIR / "04_multi_attacker_comparison.png"
    fig.savefig(out, dpi=150)
    plt.close(fig)
    print(f"  wrote {out}")


def write_summary_csv():
    names = [
        "exp1-normal",
        "exp2-attack-no-mitigation",
        "exp3-attack-mitigated",
        "exp4-intensity-low",
        "exp4-intensity-medium",
        "exp4-intensity-high",
        "exp5-threshold-200",
        "exp5-threshold-1000",
        "exp5-threshold-5000",
        "exp6-attackers-1",
        "exp6-attackers-3",
    ]
    rows = [s for s in (summarize(n) for n in names) if s is not None]

    out = RESULTS_DIR / "summary.csv"
    fieldnames = [
        "name",
        "txPackets",
        "rxPackets",
        "lostPackets",
        "lossPct",
        "throughputMbps",
        "avgDelayMs",
        "detectionDelaySec",
    ]
    with open(out, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k) for k in fieldnames})
    print(f"  wrote {out}")


if __name__ == "__main__":
    print("Generating graphs from results/...")
    fig_before_after()
    fig_intensity_sweep()
    fig_threshold_sweep()
    fig_multi_attacker()
    write_summary_csv()
    print("Done.")
