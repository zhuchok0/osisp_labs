# Lab 06 - Windows system mechanisms (Variant 10)

Cleanup of expired configuration records stored in the Windows registry.
Main APIs: `RegQueryValueEx()`, `RegDeleteValue()`.

Student ID: 47 (SEED = 1000 + 47 * 97 = 5559).

## Layout

| Folder | Content |
|---|---|
| `src/` | program source (`lab06_cleanup.cpp`) |
| `tests/` | functional tests (`run_tests.py`, unittest) |
| `data/` | experiment results (`results.csv`) |
| `scripts/` | build, experiment runner, plotting (Python) |
| `diagrams/` | algorithm diagram (Mermaid) |
| `report/` | report outline and generated graphs |

## Learning area

All writes and deletions are limited to
`HKEY_CURRENT_USER\Software\OSiSP\Lab06\Student_47\Run_<RUN_ID>`.

## Record format

One `REG_SZ` value per record: name = record id (`rec_00001`),
data = `<createdUtc>|<expiresUtc>|<payload>`, times as `YYYY-MM-DDTHH:MM:SSZ` (UTC).
A record is expired when `expiresUtc <= now`.

## Requirements

Windows, Python 3.8+, a C++ compiler (MSVC `cl` or MinGW `g++`), `matplotlib` for the graphs.

## Build and run

From `lab06-system` (for MSVC use "x64 Native Tools Command Prompt for VS"):

    python scripts/build.py
    build\lab06_cleanup.exe --mode selftest --ttl 7 --count 1000 --repeat 3

Modes: `gen`, `list`, `clean`, `selftest`, `reset`.
Options: `--student --run --ttl --count --seed --now --repeat --dry-run --log`.

## Tests, experiments, graphs

    python tests/run_tests.py
    python scripts/run_experiments.py
    python scripts/plot_results.py

`run_experiments.py` writes `data/results.csv` (standard experiment: validity 1, 7, 30 days).
For customised data set
`--seed 777 --now 2026-06-15T12:00:00Z --record-counts 5000 --output data/results_own.csv`
(`python scripts/run_experiments.py --help`).
`plot_results.py` reads the CSV and saves the two graphs to `report/`.
