"""Генерация sandbox с SEED = 1000 + STUDENT_ID*97.  Запуск: python scripts/gen_data.py --student-id 10"""
import argparse
import random
import string

from labutil import ROOT, SANDBOX

ap = argparse.ArgumentParser()
ap.add_argument("--student-id", type=int, default=10)
args = ap.parse_args()

seed = 1000 + args.student_id * 97
rng = random.Random(seed)  # фиксированный SEED -> воспроизводимое содержимое
print(f"STUDENT_ID={args.student_id} SEED={seed}")
SANDBOX.mkdir(exist_ok=True)


def make(name, size):
    written = 0
    buf = []
    with open(SANDBOX / name, "w", newline="\n", encoding="ascii") as f:
        while written < size:
            n = rng.randint(20, 79)
            buf.append("".join(rng.choices(string.ascii_lowercase, k=n)) + "\n")
            written += n + 1
            if len(buf) >= 2000:
                f.write("".join(buf))
                buf.clear()
        f.write("".join(buf))
    print(f"{name:<14} {(SANDBOX / name).stat().st_size:>12,} bytes")


for name, size in [("f_1KB.txt", 1024), ("f_100KB.txt", 100 * 1024),
                   ("f_10MB.txt", 10 * 1024 ** 2), ("f_50MB.txt", 50 * 1024 ** 2)]:
    make(name, size)

(SANDBOX / "empty.txt").write_bytes(b"")          # 0 строк
(SANDBOX / "noeol.txt").write_bytes(b"a\nb\nc")   # 3 строки, нет завершающего \n
(ROOT / "secret.txt").write_text("TOP SECRET (outside sandbox)")  # цель для проверки запрета выхода
print("Готово:", SANDBOX)
