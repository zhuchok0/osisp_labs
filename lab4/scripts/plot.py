"""Parse lab10 output files (3R/2W and 3R/5W) and build charts."""
import re
import csv
from pathlib import Path
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "data"

RE_R = re.compile(r"Readers:\s+ops=(\d+)\s+avg_wait_ms=(\d+)\s+max_wait_ms=(\d+)\s+timeouts=(\d+)")
RE_W = re.compile(r"Writers:\s+ops=(\d+)\s+avg_wait_ms=(\d+)\s+max_wait_ms=(\d+)\s+timeouts=(\d+)")
RE_T = re.compile(r"Total time, ms:\s+(\d+)")
RE_V = re.compile(r"Invariant violations:\s+(\d+)")


def parse(p: Path):
    text = p.read_text(encoding="utf-16")
    r, w, t, v = RE_R.search(text), RE_W.search(text), RE_T.search(text), RE_V.search(text)
    if not all([r, w, t, v]):
        print(f"[skip] {p.name}: cannot parse")
        return None
    return {
        "total_ms":    int(t.group(1)),
        "readers_ops": int(r.group(1)),
        "readers_avg": int(r.group(2)),
        "readers_max": int(r.group(3)),
        "readers_to":  int(r.group(4)),
        "writers_ops": int(w.group(1)),
        "writers_avg": int(w.group(2)),
        "writers_max": int(w.group(3)),
        "writers_to":  int(w.group(4)),
        "violations":  int(v.group(1)),
    }


CONFIGS = [
    ("3R/2W", "run_3r2w_*.txt", 2),
    ("3R/5W", "run_3r5w_*.txt", 5),
]


def collect():
    if not DATA.exists():
        print(f"[error] no data directory: {DATA}")
        return []
    print(f"[info] DATA = {DATA}")
    rows = []
    for label, pattern, writers in CONFIGS:
        files = sorted(DATA.glob(pattern))
        print(f"[info] {label}: {pattern} -> {len(files)} files")
        for p in files:
            row = parse(p)
            if row:
                row["config"]    = label
                row["writers_n"] = writers
                row["run"]       = p.stem
                rows.append(row)
    return rows


def save_csv(rows):
    fields = ["config", "run", "writers_n", "total_ms",
              "readers_ops", "readers_avg", "readers_max", "readers_to",
              "writers_ops", "writers_avg", "writers_max", "writers_to",
              "violations"]
    out = DATA / "summary.csv"
    with out.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for r in rows:
            w.writerow({k: r[k] for k in fields})
    print(f"[ok] wrote {out}")


def mean(rows, key):
    return sum(r[key] for r in rows) / len(rows) if rows else 0


def plot(rows):
    labels    = [c[0] for c in CONFIGS]
    writers_n = [c[2] for c in CONFIGS]
    by_cfg = {label: [r for r in rows if r["config"] == label] for label, _, _ in CONFIGS}

    plt.figure()
    means = [mean(by_cfg[c], "total_ms") for c in labels]
    plt.plot(writers_n, means, marker="o", linewidth=2)
    for x, y in zip(writers_n, means):
        plt.annotate(f"{y:.0f}", (x, y), textcoords="offset points", xytext=(0, 8))
    plt.xlabel("Число писателей")
    plt.ylabel("Общее время, мс")
    plt.title("Зависимость времени выполнения от числа писателей")
    plt.xticks(writers_n)
    plt.grid(True, alpha=0.3)
    plt.savefig(DATA / "plot_total_time.png", dpi=120, bbox_inches="tight")
    plt.close()

    plt.figure()
    r_wait = [mean(by_cfg[c], "readers_avg") for c in labels]
    w_wait = [mean(by_cfg[c], "writers_avg") for c in labels]
    plt.plot(writers_n, r_wait, marker="o", linewidth=2, label="Читатели")
    plt.plot(writers_n, w_wait, marker="s", linewidth=2, label="Писатели")
    plt.xlabel("Число писателей")
    plt.ylabel("Среднее время ожидания, мс")
    plt.title("Время ожидания по категориям участников")
    plt.xticks(writers_n)
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.savefig(DATA / "plot_wait_time.png", dpi=120, bbox_inches="tight")
    plt.close()

    plt.figure()
    r_to = [mean(by_cfg[c], "readers_to") for c in labels]
    w_to = [mean(by_cfg[c], "writers_to") for c in labels]
    plt.plot(writers_n, r_to, marker="o", linewidth=2, label="Читатели")
    plt.plot(writers_n, w_to, marker="s", linewidth=2, label="Писатели")
    plt.xlabel("Число писателей")
    plt.ylabel("Тайм-ауты (среднее за запуск)")
    plt.title("Количество тайм-аутов по категориям")
    plt.xticks(writers_n)
    plt.legend()
    plt.grid(True, alpha=0.3)
    plt.savefig(DATA / "plot_timeouts.png", dpi=120, bbox_inches="tight")
    plt.close()

    print("[ok] wrote plot_total_time.png, plot_wait_time.png, plot_timeouts.png")


def main():
    rows = collect()
    if not rows:
        print("No run_*.txt parsed. Check that data/run_3r2w_*.txt and data/run_3r5w_*.txt exist.")
        return
    save_csv(rows)
    plot(rows)


if __name__ == "__main__":
    main()