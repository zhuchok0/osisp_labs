"""Shared helpers for the Lab 06 scripts and tests."""
import os
import re
import subprocess
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = PROJECT_ROOT / "build"
DEFAULT_EXE_PATH = Path(os.environ.get("LAB06_EXE", BUILD_DIR / "lab06_cleanup.exe"))
DEFAULT_STUDENT_ID = 47


def run_program(arguments, exe_path=DEFAULT_EXE_PATH):
    """Runs lab06_cleanup.exe and returns the CompletedProcess (stdout/stderr as text)."""
    BUILD_DIR.mkdir(exist_ok=True)
    return subprocess.run(
        [str(exe_path), *[str(a) for a in arguments]],
        capture_output=True, text=True, errors="replace", cwd=PROJECT_ROOT,
    )


def extract_csv_rows(program_output):
    """Returns the lines printed as 'CSV,...' without the 'CSV,' prefix (header included)."""
    return [line[4:] for line in program_output.splitlines() if line.startswith("CSV,")]


def find_log_line(program_output, operation_tag):
    """Returns the first log line that contains the tag (e.g. 'REG_CLEAN'), or ''."""
    for line in program_output.splitlines():
        if operation_tag in line:
            return line
    return ""


def parse_key_values(log_line):
    """'... deleted=5 kept=3 time_ms=1.250' -> {'deleted': 5.0, 'kept': 3.0, 'time_ms': 1.25}"""
    return {key: float(value) for key, value in re.findall(r"(\w+)=(-?\d+(?:\.\d+)?)", log_line)}
