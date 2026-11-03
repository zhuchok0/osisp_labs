import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build"
OUT.mkdir(exist_ok=True)
TARGETS = {"server": ROOT / "src/server/server.cpp", "client": ROOT / "src/client/client.cpp"}

for name, src in TARGETS.items():
    exe = OUT / f"{name}.exe"
    if shutil.which("cl"):
        cmd = ["cl", "/nologo", "/EHsc", "/std:c++17", "/O2", "/D_CRT_SECURE_NO_WARNINGS",
               str(src), f"/Fe:{exe}", f"/Fo{OUT}\\"]
    elif shutil.which("g++"):
        cmd = ["g++", "-std=c++17", "-O2", "-D_WIN32_WINNT=0x0601", str(src), "-o", str(exe),
               "-lws2_32", "-pthread"]
    else:
        sys.exit("Не найден компилятор. Откройте 'Developer Command Prompt for VS' (cl) или установите MinGW-w64 (g++).")
    print(" ".join(cmd))
    if subprocess.call(cmd) != 0:
        sys.exit(f"Ошибка сборки {name}")
print("OK ->", OUT)
