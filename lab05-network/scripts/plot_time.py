"""График 1: время выполнения запроса в зависимости от размера файла.
Запуск: python scripts/plot_time.py [--csv data/results.csv] [--out report/fig_time.png] [--op COUNT_LINES]
Для каждого числа клиентов строятся средний RTT (сплошная линия) и среднее серверное время (пунктир)."""
import argparse
import sys
from collections import defaultdict
from statistics import mean

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from plotdata import ROOT, load_results, size_label

ap = argparse.ArgumentParser()
ap.add_argument("--csv", default=str(ROOT / "data" / "results.csv"))
ap.add_argument("--out", default=str(ROOT / "report" / "fig_time.png"))
ap.add_argument("--op", default="COUNT_LINES", choices=["SIZE", "COUNT_LINES", "READ_FIRST"])
args = ap.parse_args()

rows = [r for r in load_results(args.csv) if r["op"] == args.op]
if not rows:
    sys.exit(f"В {args.csv} нет данных для операции {args.op}")

rtt, srv = defaultdict(list), defaultdict(list)   # (клиентов, размер) -> значения по всем запускам и клиентам
for r in rows:
    rtt[(r["clients"], r["size"])].append(r["rtt_ms"])
    srv[(r["clients"], r["size"])].append(r["server_ms"])

sizes = sorted({k[1] for k in rtt})
fig, ax = plt.subplots(figsize=(8, 5))
for n in sorted({k[0] for k in rtt}):
    xs = [s for s in sizes if (n, s) in rtt]
    ax.plot(xs, [mean(rtt[(n, s)]) for s in xs], "o-", label=f"RTT, клиентов: {n}")
    ax.plot(xs, [mean(srv[(n, s)]) for s in xs], "s--", label=f"Время на сервере, клиентов: {n}")

ax.set_xscale("log")
ax.set_yscale("log")
ax.set_xticks(sizes)
ax.set_xticklabels([size_label(s) for s in sizes])
ax.minorticks_off()
ax.set_title(f"Время выполнения {args.op} в зависимости от размера файла")
ax.set_xlabel("Размер файла (логарифмическая шкала)")
ax.set_ylabel("Среднее время запроса, мс (логарифмическая шкала)")
ax.grid(True, which="both", alpha=0.3)
ax.legend()
fig.tight_layout()

fig.savefig(args.out, dpi=150)
print("Сохранено:", args.out)
