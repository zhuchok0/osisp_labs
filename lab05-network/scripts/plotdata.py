"""Чтение data/results.csv для скриптов построения графиков."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# метка конфигурации: c<клиентов>_f_<число><KB|MB>.txt_<операция>_r<запуск>
_LABEL = re.compile(r"^c(\d+)_f_(\d+)(KB|MB)\.txt_(SIZE|COUNT_LINES|READ_FIRST)_r(\d+)$")


def load_results(path):
    """Возвращает список словарей: одна запись соответствует одному клиентскому процессу."""
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            m = _LABEL.match(r["label"])
            if not m:
                continue
            n, num, unit, op, run = m.groups()
            rows.append({
                "clients": int(n),
                "size": int(num) * (1024 if unit == "KB" else 1024 ** 2),  # номинальный размер файла, байт
                "op": op,
                "run": int(run),
                "sent": int(r["sent"]),
                "received": int(r["received"]),
                "ok": int(r["ok"]),
                "err": int(r["err"]),
                "timeouts": int(r["timeouts"]),
                "connect_ms": float(r["connect_ms"]),
                "rtt_ms": float(r["avg_rtt_ms"]),
                "server_ms": float(r["avg_server_ms"]),
                "total_ms": float(r["total_ms"]),
                "bytes_out": int(r["bytes_out"]),
                "bytes_in": int(r["bytes_in"]),
            })
    return rows


def size_label(size):
    return f"{size // 1024} КБ" if size < 1024 ** 2 else f"{size // 1024 ** 2} МБ"
