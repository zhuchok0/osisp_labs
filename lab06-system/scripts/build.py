"""Builds src/lab06_cleanup.cpp into build/lab06_cleanup.exe.
Uses MSVC (cl) if it is on PATH (run from an "x64 Native Tools Command Prompt"),
otherwise MinGW g++.
Usage: python scripts/build.py
"""
import shutil
import subprocess
import sys

from lab06_common import BUILD_DIR, PROJECT_ROOT


def main():
    BUILD_DIR.mkdir(exist_ok=True)
    # Relative paths, with PROJECT_ROOT as the working directory (avoids quoting problems).
    if shutil.which("cl"):
        command = ["cl", "/nologo", "/EHsc", "/O2", "/W4",
                   "/Fe:build\\lab06_cleanup.exe", "/Fo:build\\",
                   "src\\lab06_cleanup.cpp", "advapi32.lib"]
    elif shutil.which("g++"):
        command = ["g++", "-O2", "-Wall", "src/lab06_cleanup.cpp",
                   "-o", "build/lab06_cleanup.exe", "-ladvapi32"]
    else:
        sys.exit("No compiler found: run from a Visual Studio command prompt (cl) or install MinGW (g++).")

    print(" ".join(command))
    result = subprocess.run(command, cwd=PROJECT_ROOT)
    if result.returncode != 0:
        sys.exit("Build FAILED")
    print("Built: build/lab06_cleanup.exe")


if __name__ == "__main__":
    main()
