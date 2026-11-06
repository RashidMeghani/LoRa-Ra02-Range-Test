#!/usr/bin/env python3
"""Plot a field range test from a CSV.

CSV columns (see data/range_log_template.csv):
    distance_km,rssi_dbm,snr_db,sent,received,notes
Usage:
    python3 tools/plot_range.py data/my_test.csv [out.png]
Needs matplotlib (pip install matplotlib); without it a text table is printed.
"""
import csv
import sys


def load(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            if not r.get("distance_km"):
                continue
            rows.append({
                "km": float(r["distance_km"]),
                "rssi": float(r["rssi_dbm"]) if r.get("rssi_dbm") else None,
                "snr": float(r["snr_db"]) if r.get("snr_db") else None,
                "sent": int(r["sent"]) if r.get("sent") else 0,
                "recv": int(r["received"]) if r.get("received") else 0,
            })
    return sorted(rows, key=lambda x: x["km"])


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    rows = load(sys.argv[1])
    print("km    RSSI   SNR   delivered")
    for r in rows:
        pct = 100.0 * r["recv"] / r["sent"] if r["sent"] else float("nan")
        print(f"{r['km']:5.1f} {r['rssi'] if r['rssi'] is not None else '--':>6} "
              f"{r['snr'] if r['snr'] is not None else '--':>5}   "
              f"{r['recv']}/{r['sent']} ({pct:.0f}%)")
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return
    km = [r["km"] for r in rows]
    fig, ax1 = plt.subplots(figsize=(8, 4.5))
    ax1.plot(km, [r["rssi"] for r in rows], "o-", color="#1f77b4", label="RSSI")
    ax1.set_xlabel("Distance (km)")
    ax1.set_ylabel("RSSI (dBm)", color="#1f77b4")
    ax2 = ax1.twinx()
    ax2.bar(km, [100.0 * r["recv"] / r["sent"] if r["sent"] else 0 for r in rows],
            width=1.5, alpha=0.25, color="#2ca02c")
    ax2.set_ylabel("Delivered (%)", color="#2ca02c")
    ax2.set_ylim(0, 105)
    ax1.set_title("LoRa range test")
    ax1.grid(alpha=0.3)
    out = sys.argv[2] if len(sys.argv) > 2 else "range_plot.png"
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print("saved", out)


if __name__ == "__main__":
    main()
