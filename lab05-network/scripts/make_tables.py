"""Таблицы отчета по data/results.csv.
Запуск: python scripts/make_tables.py [--csv data/results.csv] [--metric rtt|server|total] [--op SIZE|COUNT_LINES|READ_FIRST]

Таблица 2: значение показателя в каждом из запусков и среднее. Значение запуска - среднее по клиентам этого запуска.
Дополнительная таблица: суммы по всем запускам и клиентам (запросы, успехи, ошибки, тайм-ауты, байты) и средние времена.
Результат: report/table2.md, report/table_extra.md (для отчета) и data/table2.csv, data/table_extra.csv (исходные данные)."""
import argparse
import csv
import sys
from collections import defaultdict
from statistics import mean

from plotdata import ROOT, load_results, size_label

METRICS = {"rtt": ("rtt_ms", "средний RTT запроса"),
           "server": ("server_ms", "среднее время обработки на сервере"),
           "total": ("total_ms", "полное время работы клиента")}
OPS = ["SIZE", "COUNT_LINES", "READ_FIRST"]

ap = argparse.ArgumentParser()
ap.add_argument("--csv", default=str(ROOT / "data" / "results.csv"))
ap.add_argument("--metric", default="rtt", choices=list(METRICS))
ap.add_argument("--op", default=None, choices=OPS)
ap.add_argument("--report-dir", default=str(ROOT / "report"))
ap.add_argument("--data-dir", default=str(ROOT / "data"))
args = ap.parse_args()

field, metric_name = METRICS[args.metric]
rows = [r for r in load_results(args.csv) if args.op in (None, r["op"])]
if not rows:
    sys.exit("В файле нет подходящих данных")

cfg = defaultdict(list)  # (операция, клиентов, размер) -> записи
for r in rows:
    cfg[(OPS.index(r["op"]), r["clients"], r["size"])].append(r)
keys = sorted(cfg)


def name(k):
    op, n, size = k
    return f"{n} {'клиент' if n == 1 else 'клиента'}, {size_label(size)}, {OPS[op]}"


def comma(x, nd=3):
    return f"{x:.{nd}f}".replace(".", ",")


def write_md(path, title, header, body):
    lines = [title, "", "| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(r) + " |" for r in body]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_csv(path, header, body):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        w.writerows(body)


from pathlib import Path
report, data = Path(args.report_dir), Path(args.data_dir)

# ---- Таблица 2
runs_all = sorted({r["run"] for r in rows})
t2_head = ["Конфигурация"] + [f"Запуск {i}, мс" for i in runs_all] + ["Среднее, мс"]
t2_csv, t2_md = [], []
for idx, k in enumerate(keys, 1):
    per_run = []
    for run in runs_all:
        vals = [r[field] for r in cfg[k] if r["run"] == run]
        per_run.append(mean(vals) if vals else None)
    present = [v for v in per_run if v is not None]
    avg = mean(present) if present else None
    t2_csv.append([f"{idx}. {name(k)}"] + [f"{v:.3f}" if v is not None else "" for v in per_run]
                  + [f"{avg:.3f}" if avg is not None else ""])
    t2_md.append([f"{idx}. {name(k)}"] + [comma(v) if v is not None else "" for v in per_run]
                 + [comma(avg) if avg is not None else ""])
write_csv(data / "table2.csv", t2_head, t2_csv)
write_md(report / "table2.md",
         f"Таблица 2 – Экспериментальные данные ({metric_name}, мс; значение запуска усреднено по клиентам)",
         t2_head, t2_md)

# ---- Дополнительная таблица
ex_head = ["Конфигурация", "Запросов отправлено", "Успешных (OK)", "Ошибок (ERR)", "Тайм-аутов",
           "Передано клиентом, байт", "Получено клиентом, байт", "Время подключения, мс", "Время на сервере, мс"]
ex_csv, ex_md = [], []
for idx, k in enumerate(keys, 1):
    rs = cfg[k]
    vals = [sum(r["sent"] for r in rs), sum(r["ok"] for r in rs), sum(r["err"] for r in rs),
            sum(r["timeouts"] for r in rs), sum(r["bytes_out"] for r in rs), sum(r["bytes_in"] for r in rs)]
    conn, srv = mean(r["connect_ms"] for r in rs), mean(r["server_ms"] for r in rs)
    ex_csv.append([f"{idx}. {name(k)}"] + vals + [f"{conn:.3f}", f"{srv:.3f}"])
    ex_md.append([f"{idx}. {name(k)}"] + [str(v) for v in vals] + [comma(conn), comma(srv)])
write_csv(data / "table_extra.csv", ex_head, ex_csv)
write_md(report / "table_extra.md",
         "Таблица 4 – Успешные запросы, ошибки, переданные данные (суммы по всем запускам и клиентам)",
         ex_head, ex_md)
print("Сохранено:", report / "table2.md", report / "table_extra.md", data / "table2.csv", data / "table_extra.csv")
