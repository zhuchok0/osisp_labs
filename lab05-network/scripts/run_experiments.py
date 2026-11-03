"""Эксперименты: 1 и 3 клиента x файлы x команды x 3 запуска -> data/results.csv, data/server.log.
Запуск: python scripts/run_experiments.py --student-id 10 --count 20"""
import argparse
import subprocess

from labutil import ROOT, find_bin, start_server, stop_server

ap = argparse.ArgumentParser()
ap.add_argument("--student-id", type=int, default=10)
ap.add_argument("--runs", type=int, default=3)
ap.add_argument("--count", type=int, default=20, help="запросов на клиента за запуск")
ap.add_argument("--bin", default=None)
args = ap.parse_args()

bin_dir = find_bin(args.bin)
port = 20000 + args.student_id
data = ROOT / "data"
data.mkdir(exist_ok=True)
csv = data / "results.csv"
csv.write_text("label,client_id,cmd,sent,received,ok,err,timeouts,connect_ms,total_ms,"
               "avg_rtt_ms,avg_server_ms,bytes_out,bytes_in\n")

FILES = ["f_1KB.txt", "f_100KB.txt", "f_10MB.txt", "f_50MB.txt"]
OPS = [("SIZE", lambda f: f"SIZE {f}"),
       ("COUNT_LINES", lambda f: f"COUNT_LINES {f}"),
       ("READ_FIRST", lambda f: f"READ_FIRST {f} 100")]

server = start_server(bin_dir, port, log=data / "server.log")
try:
    for n in (1, 3):
        for f in FILES:
            for op_name, make_cmd in OPS:
                for run in range(1, args.runs + 1):
                    label = f"c{n}_{f}_{op_name}_r{run}"
                    procs = [subprocess.Popen(
                        [str(bin_dir / "client.exe"), "--host", "127.0.0.1", "--port", str(port),
                         "--id", str(i), "--count", str(args.count), "--timeout", "60000", "--quiet",
                         "--label", label, "--csv", str(csv), "--cmd", make_cmd(f)],
                        stdout=subprocess.DEVNULL) for i in range(1, n + 1)]  # параллельно
                    codes = [p.wait() for p in procs]
                    print("done", label, "" if not any(codes) else f"exit={codes}")
finally:
    stop_server(server)
print("Results:", csv)
