"""Функциональные тесты варианта 10.  Запуск: python tests/run_tests.py --student-id 10"""
import argparse
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "scripts"))
from labutil import find_bin, recv_frame, send_frame, start_server, stop_server  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("--student-id", type=int, default=10)
ap.add_argument("--bin", default=None)
args = ap.parse_args()

bin_dir = find_bin(args.bin)
client = str(bin_dir / "client.exe")
port = 20000 + args.student_id
passed = failed = 0


def check(name, cond):
    global passed, failed
    print(("PASS  " if cond else "FAIL  ") + name)
    passed, failed = (passed + 1, failed) if cond else (passed, failed + 1)


def run(cmd, p=None, extra=()):
    r = subprocess.run([client, "--host", "127.0.0.1", "--port", str(p or port), "--id", "99",
                        "--cmd", cmd, *extra], capture_output=True, text=True)
    return r.stdout + r.stderr, r.returncode


server = start_server(bin_dir, port)
slow = start_server(bin_dir, port + 1, extra=["--delay", "1500"])
try:
    # --- базовые команды
    check("HELP", "OK 200" in run("HELP")[0])
    check("LIST содержит f_1KB.txt", "f_1KB.txt" in run("LIST")[0])
    check("SIZE f_1KB.txt", "OK 200" in run("SIZE f_1KB.txt")[0])
    check("COUNT_LINES noeol = 3", bool(re.search(r"(?m)^3\s*$", run("COUNT_LINES noeol.txt")[0])))
    check("COUNT_LINES empty = 0", bool(re.search(r"(?m)^0\s*$", run("COUNT_LINES empty.txt")[0])))
    out = run("READ_FIRST noeol.txt 2")[0]
    check("READ_FIRST noeol 2", re.search(r"(?m)^a\s*$", out) and re.search(r"(?m)^b\s*$", out)
          and not re.search(r"(?m)^c\s*$", out))

    # --- отсутствующий файл
    check("SIZE отсутствующего -> 404", "ERR 404" in run("SIZE no_such_file.txt")[0])
    check("COUNT_LINES отсутствующего -> 404", "ERR 404" in run("COUNT_LINES no_such.txt")[0])

    # --- некорректный путь / выход за sandbox
    bad_paths = ["../secret.txt", "..\\secret.txt", "..", "....//secret.txt", "%2e%2e%2fsecret.txt",
                 "C:\\Windows\\win.ini", "\\\\localhost\\c$\\Windows\\win.ini", "/etc/passwd",
                 "f_1KB.txt:stream", "CON", "NUL", "f_1KB.txt.", "sub/x.txt"]
    for bad in bad_paths:
        check(f"путь {bad!r} -> 403", "ERR 403" in run(f"SIZE {bad}")[0])
    check("READ_FIRST ../secret.txt -> 403", "ERR 403" in run("READ_FIRST ../secret.txt 5")[0])
    check("секрет не утёк", "TOP SECRET" not in run("READ_FIRST ../secret.txt 5")[0])

    # --- неверные команды и аргументы
    check("неизвестная команда -> 400", "ERR 400" in run("DELETE f_1KB.txt")[0])
    check("SIZE без аргумента -> 400", "ERR 400" in run("SIZE")[0])
    for n in ("0", "abc", "-1", "1001"):
        check(f"READ_FIRST n={n} -> 400", "ERR 400" in run(f"READ_FIRST f_1KB.txt {n}")[0])

    # --- неверная длина кадра
    s = socket.create_connection(("127.0.0.1", port))
    s.sendall(b"\xff\xff\xff\xff")
    check("кадр 4 ГБ -> 413", b"ERR 413" in recv_frame(s))
    s.close()

    # --- частичная отправка (заголовок и тело приходят порциями)
    p = b"V1 1 99 HELP"
    s = socket.create_connection(("127.0.0.1", port))
    s.sendall(len(p).to_bytes(4, "big")); time.sleep(0.3)
    s.sendall(p[:3]); time.sleep(0.3)
    s.sendall(p[3:])
    check("частичная отправка собрана", b"OK 200" in recv_frame(s))
    s.close()

    # --- тайм-аут клиента (сервер отвечает через 1.5 с, клиент ждёт 0.5 с)
    out, code = run("HELP", p=port + 1, extra=["--timeout", "500"])
    check("тайм-аут клиента (exit 1)", "TIMEOUT" in out and code == 1)

    # --- сервер недоступен
    out, code = run("HELP", p=29999)
    check("сервер недоступен (exit 2)", code == 2)

    # --- одновременные запросы: 3 клиента по 50 запросов
    procs = [subprocess.Popen([client, "--port", str(port), "--id", str(i), "--count", "50", "--quiet",
                               "--cmd", "COUNT_LINES f_100KB.txt"], stdout=subprocess.DEVNULL)
             for i in (1, 2, 3)]
    check("3 клиента одновременно, все exit 0", all(q.wait() == 0 for q in procs))
finally:
    stop_server(server)
    stop_server(slow)

print(f"\nPassed: {passed}  Failed: {failed}")
sys.exit(0 if failed == 0 else 1)
