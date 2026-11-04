"""График 2: пропускная способность в зависимости от размера файла и числа клиентов.
Запуск: python scripts/plot_throughput.py [--csv data/results.csv] [--out report/fig_throughput.png]
                                          [--op COUNT_LINES] [--metric rps|mbps]
Для запуска (n клиентов параллельно): запросов = сумма received; время = максимальное total_ms среди клиентов.
rps  - запросов в секунду; mbps - МБ/с обработанных данных файла (rps * размер файла), осмысленно для COUNT_LINES."""
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
ap.add_argument("--out", default=str(ROOT / "report" / "fig_throughput.png"))
ap.add_argument("--op", default="COUNT_LINES", choices=["SIZE", "COUNT_LINES", "READ_FIRST"])
ap.add_argument("--metric", default="rps", choices=["rps", "mbps"])
args = ap.parse_args()

rows = [r for r in load_results(args.csv) if r["op"] == args.op]
if not rows:
    sys.exit(f"В {args.csv} нет данных для операции {args.op}")

runs = defaultdict(list)  # (клиентов, размер, запуск) -> записи клиентов этого запуска
for r in rows:
    runs[(r["clients"], r["size"], r["run"])].append(r)

value = defaultdict(list)  # (клиентов, размер) -> значения по запускам
for (n, size, _run), rs in runs.items():
    wall_s = max(x["total_ms"] for x in rs) / 1000.0
    rps = sum(x["received"] for x in rs) / wall_s
    value[(n, size)].append(rps if args.metric == "rps" else rps * size / 1024 ** 2)

sizes = sorted({k[1] for k in value})
clients = sorted({k[0] for k in value})
width = 0.8 / len(clients)
fig, ax = plt.subplots(figsize=(8, 5))
for i, n in enumerate(clients):
    xs = [j + (i - (len(clients) - 1) / 2) * width for j in range(len(sizes))]
    ys = [mean(value[(n, s)]) if (n, s) in value else 0 for s in sizes]
    ax.bar(xs, ys, width, label=f"Клиентов: {n}")

ax.set_xticks(range(len(sizes)))
ax.set_xticklabels([size_label(s) for s in sizes])
ax.set_yscale("log")
unit = "запросов в секунду" if args.metric == "rps" else "МБ/с"
ax.set_title(f"Пропускная способность {args.op}: суммарно по всем клиентам")
ax.set_xlabel("Размер файла")
ax.set_ylabel(f"Пропускная способность, {unit} (лог. шкала)")
ax.grid(True, axis="y", alpha=0.3)
ax.legend()
fig.tight_layout()
fig.savefig(args.out, dpi=150)
print("Сохранено:", args.out)
