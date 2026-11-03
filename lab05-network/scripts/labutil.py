import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SANDBOX = ROOT / "sandbox"


def find_bin(bin_dir=None):
    """Папка со server.exe и client.exe (build, build/Release, build/Debug или --bin)."""
    candidates = [bin_dir] if bin_dir else ["build", "build/Release", "build/Debug"]
    for c in candidates:
        d = (ROOT / c).resolve()
        if (d / "server.exe").exists() and (d / "client.exe").exists():
            return d
    sys.exit("server.exe / client.exe не найдены. Сначала: python scripts/build.py")


def wait_port(port, timeout=5.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            socket.create_connection(("127.0.0.1", port), 0.2).close()
            return
        except OSError:
            time.sleep(0.1)
    raise RuntimeError(f"сервер не начал слушать порт {port}")


def start_server(bin_dir, port, extra=(), log=None):
    cmd = [str(bin_dir / "server.exe"), "--host", "127.0.0.1", "--port", str(port),
           "--sandbox", str(SANDBOX), *map(str, extra)]
    out = open(log, "w") if log else subprocess.DEVNULL
    proc = subprocess.Popen(cmd, stdout=out, stderr=subprocess.STDOUT)
    wait_port(port)
    return proc


def stop_server(proc):
    """Принудительная остановка (штатное завершение по Ctrl+C проверяется вручную)."""
    if proc and proc.poll() is None:
        proc.terminate()
        proc.wait()


def send_frame(sock, payload: bytes):
    sock.sendall(struct.pack(">I", len(payload)) + payload)


def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            break
        buf += chunk
    return buf


def recv_frame(sock):
    hdr = recv_exact(sock, 4)
    if len(hdr) < 4:
        return b""
    (length,) = struct.unpack(">I", hdr)
    return recv_exact(sock, length)
