"""Builds the two graphs required by section 6.4 from data/results.csv.
Usage: python scripts/plot_results.py [path/to/results.csv]
Needs: matplotlib
"""
import csv
import sys
from collections import defaultdict
from pathlib import Path
from statistics import mean

import matplotlib.pyplot as plt

PROJECT_ROOT = Path(__file__).resolve().parent.parent
csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else PROJECT_ROOT / "data" / "results.csv"
report_dir = PROJECT_ROOT / "report"

with open(csv_path, newline="") as csv_file:
    rows = list(csv.DictReader(csv_file))
if not rows:
    sys.exit("results.csv is empty - run scripts/run_experiments.py first")

times_ms = defaultdict(list)   # (validity_days, record_count) -> time of each run
outcome = {}                   # (validity_days, record_count) -> (expired_found, kept)
for row in rows:
    key = (int(row["validity_days"]), int(row["record_count"]))
    times_ms[key].append(float(row["time_ms"]))
    outcome[key] = (int(row["expired_found"]), int(row["kept"]))   # deterministic for a fixed seed

validity_days_list = sorted({key[0] for key in times_ms})
record_counts_list = sorted({key[1] for key in times_ms})

# Graph 1: cleanup time vs number of records, one line per validity period.
plt.figure(figsize=(7, 4.5))
for days in validity_days_list:
    counts = [c for c in record_counts_list if (days, c) in times_ms]
    plt.plot(counts, [mean(times_ms[(days, c)]) for c in counts], marker="o", label=f"validity {days} d")
plt.xscale("log")
plt.yscale("log")
plt.title("Cleanup time vs number of records")
plt.xlabel("Number of records in the key, pcs")
plt.ylabel("Cleanup time (mean of runs), ms")
plt.grid(True, which="both", alpha=0.3)
plt.legend()
plt.tight_layout()
plt.savefig(report_dir / "graph1_time_vs_records.png", dpi=200)

# Graph 2: expired (deleted) vs kept records for the largest data set.
largest_count = record_counts_list[-1]
expired_counts = [outcome[(d, largest_count)][0] for d in validity_days_list]
kept_counts = [outcome[(d, largest_count)][1] for d in validity_days_list]
positions = range(len(validity_days_list))
plt.figure(figsize=(7, 4.5))
plt.bar([p - 0.2 for p in positions], expired_counts, width=0.4, label="expired (deleted)")
plt.bar([p + 0.2 for p in positions], kept_counts, width=0.4, label="valid (kept)")
plt.xticks(list(positions), [f"{d} d" for d in validity_days_list])
plt.title(f"Expired and kept records, {largest_count} records")
plt.xlabel("Validity period, days")
plt.ylabel("Number of records, pcs")
plt.legend()
plt.tight_layout()
plt.savefig(report_dir / "graph2_expired_vs_kept.png", dpi=200)
print("Saved report/graph1_time_vs_records.png and report/graph2_expired_vs_kept.png")
