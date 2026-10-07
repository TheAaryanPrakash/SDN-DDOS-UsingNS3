"""
Shared helpers for turning this project's real simulation output (flowmon.xml,
detections.csv) into the numbers the Phase 12 graphs plot. No fabricated or
estimated data anywhere here -- every function just reads and summarizes
files that a simulation run actually produced.

Only the Python standard library is used here (xml.etree, csv) so there is
nothing extra to install beyond matplotlib itself.
"""

import csv
import xml.etree.ElementTree as ET
from pathlib import Path

# Every experiment in this project uses exactly 3 legitimate clients, always
# assigned 10.1.1.1 - 10.1.1.3 (clients are created first, before any
# attacker nodes, in every simulation file). This lets us tell "legitimate"
# and "attacker" flows apart reliably across every experiment without having
# to know how many attackers a given run had.
CLIENT_IPS = {"10.1.1.1", "10.1.1.2", "10.1.1.3"}
APP_PORT = "9"  # the UDP port both legitimate clients and attackers target

RESULTS_DIR = Path(__file__).resolve().parents[2] / "results"
GRAPHS_DIR = Path(__file__).resolve().parents[2] / "graphs"


def load_flows(flowmon_path):
    """Parse one flowmon.xml into a list of flow dicts.

    Only flows on the application port (9) are returned -- this excludes
    the OpenFlow control-channel traffic between switch and controller
    (its own 10.100.0.0/24 subnet), which FlowMonitor also captures since
    it monitors every node with an IPv4 stack. See documentation/environment.md.
    """
    tree = ET.parse(flowmon_path)
    root = tree.getroot()

    classifier = {}
    for f in root.find("Ipv4FlowClassifier").findall("Flow"):
        classifier[f.get("flowId")] = {
            "src": f.get("sourceAddress"),
            "dst": f.get("destinationAddress"),
            "dport": f.get("destinationPort"),
        }

    flows = []
    for f in root.find("FlowStats").findall("Flow"):
        fid = f.get("flowId")
        meta = classifier.get(fid)
        if not meta or meta["dport"] != APP_PORT:
            continue

        tx = int(f.get("txPackets"))
        rx = int(f.get("rxPackets"))
        lost = int(f.get("lostPackets"))
        rx_bytes = int(f.get("rxBytes"))
        delay_sum_ns = _parse_ns3_time(f.get("delaySum"))

        flows.append(
            {
                "flowId": fid,
                "src": meta["src"],
                "dst": meta["dst"],
                "txPackets": tx,
                "rxPackets": rx,
                "lostPackets": lost,
                "rxBytes": rx_bytes,
                "delaySumNs": delay_sum_ns,
                "isLegit": meta["src"] in CLIENT_IPS,
            }
        )
    return flows


def _parse_ns3_time(value):
    """ns-3 writes times like '+123456.0ns' -- strip the sign/unit."""
    if value is None:
        return 0.0
    return float(value.replace("+", "").replace("ns", ""))


def legit_summary(flows, duration_sec):
    """Aggregate metrics across only the legitimate-client flows.

    Returns throughput in Mbps (received bytes / duration), loss as a
    percentage of packets sent, and average per-packet delay in ms.
    """
    legit = [f for f in flows if f["isLegit"]]
    tx = sum(f["txPackets"] for f in legit)
    rx = sum(f["rxPackets"] for f in legit)
    lost = sum(f["lostPackets"] for f in legit)
    rx_bytes = sum(f["rxBytes"] for f in legit)
    delay_sum_ns = sum(f["delaySumNs"] for f in legit)

    throughput_mbps = (rx_bytes * 8 / 1e6) / duration_sec if duration_sec else 0.0
    loss_pct = (lost / tx * 100) if tx else 0.0
    avg_delay_ms = (delay_sum_ns / rx / 1e6) if rx else 0.0

    return {
        "txPackets": tx,
        "rxPackets": rx,
        "lostPackets": lost,
        "throughputMbps": throughput_mbps,
        "lossPct": loss_pct,
        "avgDelayMs": avg_delay_ms,
    }


def load_detections(detections_csv_path):
    """Return the rows of a detections.csv as a list of dicts, or [] if the
    experiment produced no detection events (e.g. pure normal traffic)."""
    path = Path(detections_csv_path)
    if not path.exists():
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


def first_detection_time(detections):
    """Time (sim seconds) of the first DETECTED*/CLEARED* row, or None."""
    for row in detections:
        if row["Event"].startswith("DETECTED"):
            return float(row["TimeSec"])
    return None


def experiment_flowmon(name):
    return RESULTS_DIR / name / "flowmon.xml"


def experiment_detections(name):
    return RESULTS_DIR / name / "detections.csv"
